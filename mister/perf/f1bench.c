/*
 * f1bench: the MiSTer's memory and storage, measured for the performance model (phase 1).
 *   f1bench FILE DIR
 * 1. memcpy in cached RAM: a block inside the L2 (256 KB) and a frame-sized one (8 MB), MB/s
 * 2. the framebuffer (/dev/fb0, mapped as ES maps it): reading it to RAM, and writing back what was read
 *    (the screen keeps its pixels), MB/s
 * 3. FILE (a large file on the SD card): 4 KB reads at random places with O_DIRECT (no page cache), latency
 *    percentiles; and 1 MB sequential reads, MB/s
 * 4. DIR: one listing (opendir/readdir), entries and ms (with the page cache as it is)
 * Close ES first.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static double nowMs(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static int cmpd(const void* a, const void* b)
{
	const double x = *(const double*)a, y = *(const double*)b;
	return x < y ? -1 : x > y;
}

static double pct(double* v, int n, double p)
{
	int i = (int)(p * (n - 1) + 0.5);
	return v[i < 0 ? 0 : i >= n ? n - 1 : i];
}

static volatile unsigned sink;

static void copyBench(const char* name, size_t bytes, int reps)
{
	uint8_t* a = aligned_alloc(64, bytes);
	uint8_t* b = aligned_alloc(64, bytes);
	memset(a, 1, bytes);
	memset(b, 2, bytes);
	double t[64];
	for (int r = 0; r < reps; r++)
	{
		const double s = nowMs();
		memcpy(b, a, bytes);
		// the copy kept: the compiler may not drop a memcpy whose result looks unused
		__asm__ volatile("" : : "r"(b), "r"(a) : "memory");
		t[r] = nowMs() - s;
		sink += b[(size_t)r * 4093 % bytes];
	}
	qsort(t, reps, sizeof(double), cmpd);
	printf("memcpy %s (%zu KB): p50 %.0f MB/s, best %.0f MB/s\n", name, bytes / 1024, bytes / 1e6 / (pct(t, reps, 0.5) / 1e3),
		bytes / 1e6 / (t[0] / 1e3));
	free(a);
	free(b);
}

int main(int argc, char** argv)
{
	if (argc < 3)
	{
		fprintf(stderr, "usage: %s FILE DIR\n", argv[0]);
		return 2;
	}

	copyBench("in L2", 256 * 1024, 50);
	copyBench("frame", 8 * 1024 * 1024, 20);

	int fb = open("/dev/fb0", O_RDWR);
	if (fb >= 0)
	{
		struct fb_var_screeninfo var;
		struct fb_fix_screeninfo fix;
		if (ioctl(fb, FBIOGET_VSCREENINFO, &var) == 0 && ioctl(fb, FBIOGET_FSCREENINFO, &fix) == 0)
		{
			const size_t bytes = (size_t)fix.line_length * var.yres;
			uint8_t* map = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
			uint8_t* ram = aligned_alloc(64, bytes);
			if (map != MAP_FAILED && ram != NULL)
			{
				double rt[10], wt[10];
				for (int r = 0; r < 10; r++)
				{
					double s = nowMs();
					memcpy(ram, map, bytes);
					__asm__ volatile("" : : "r"(ram), "r"(map) : "memory");
					rt[r] = nowMs() - s;
					s = nowMs();
					memcpy(map, ram, bytes); // the same pixels back
					__asm__ volatile("" : : "r"(ram), "r"(map) : "memory");
					wt[r] = nowMs() - s;
				}
				qsort(rt, 10, sizeof(double), cmpd);
				qsort(wt, 10, sizeof(double), cmpd);
				printf("framebuffer %ux%u, %u bits, %u bytes a row (%zu KB): read to RAM p50 %.1f ms = %.0f MB/s; "
					"write from RAM p50 %.1f ms = %.0f MB/s\n", var.xres, var.yres, var.bits_per_pixel, fix.line_length,
					bytes / 1024, pct(rt, 10, 0.5), bytes / 1e6 / (pct(rt, 10, 0.5) / 1e3), pct(wt, 10, 0.5),
					bytes / 1e6 / (pct(wt, 10, 0.5) / 1e3));
				munmap(map, bytes);
			}
			free(ram);
		}
		close(fb);
	}

	int f = open(argv[1], O_RDONLY | O_DIRECT);
	struct stat st;
	if (f >= 0 && fstat(f, &st) == 0 && st.st_size > 64 * 1024 * 1024)
	{
		void* buf = aligned_alloc(4096, 1 << 20);
		enum { N = 300 };
		double lat[N];
		srand(12345);
		const uint64_t blocks = (uint64_t)st.st_size / 4096 - 1;
		for (int i = 0; i < N; i++)
		{
			const uint64_t at = ((((uint64_t)rand() << 31) ^ (uint64_t)rand()) % blocks) * 4096;
			const double s = nowMs();
			if (pread(f, buf, 4096, (off_t)at) != 4096)
				lat[i] = -1;
			else
				lat[i] = nowMs() - s;
		}
		qsort(lat, N, sizeof(double), cmpd);
		printf("SD random 4 KB reads (O_DIRECT, %d, file of %lld MB): p50 %.2f ms, p95 %.2f ms, p99 %.2f ms, max %.2f ms\n", N,
			(long long)(st.st_size >> 20), pct(lat, N, 0.5), pct(lat, N, 0.95), pct(lat, N, 0.99), lat[N - 1]);
		const double s = nowMs();
		size_t got = 0;
		for (int i = 0; i < 32; i++)
		{
			const ssize_t r = pread(f, buf, 1 << 20, (off_t)i << 20);
			if (r <= 0)
				break;
			got += (size_t)r;
		}
		const double ms = nowMs() - s;
		printf("SD sequential 1 MB reads (O_DIRECT): %zu MB in %.0f ms = %.1f MB/s\n", got >> 20, ms, got / 1e6 / (ms / 1e3));
		close(f);
	}
	else
		printf("SD: %s not found or smaller than 64 MB\n", argv[1]);

	const double s = nowMs();
	DIR* d = opendir(argv[2]);
	int entries = 0;
	if (d != NULL)
	{
		while (readdir(d) != NULL)
			entries++;
		closedir(d);
	}
	printf("listing %s: %d entries in %.1f ms\n", argv[2], entries, nowMs() - s);
	return 0;
}
