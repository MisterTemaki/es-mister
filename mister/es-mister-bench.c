/*
 * es-mister-bench: how fast the DE10-Nano's ARM draws the frames ES draws, measured on the MiSTer itself.
 *
 *   es-mister-bench [--reps N] [--mem] [--force]
 *
 * A 1920x1080 frame like the one of a system change in the carousel (a clear, two backgrounds faded one
 * into the other, a picture with its own alpha over them, the copy to the screen), drawn with the same
 * NEON code as ES's software renderer, in the ways the renderer could draw it:
 *   1. the memory alone: read, write and copy a frame;
 *   2. one layer, with the memory asked for ahead (PLD) at several distances, on 1 and 2 threads;
 *   3. the whole frame: a layer at a time (package 16), a few rows at a time through all the layers
 *      (package 17), and a few rows at a time drawn apart and copied to the screen at once;
 *   4. the copy to the screen alone.
 * Every time is the median of the repetitions (and the best). The framebuffer (/dev/fb0) is the screen
 * when it is 1920x1080 at 32 bits (as ES leaves it); its pixels are put back at the end. --mem: a frame
 * in memory as the screen. Close ES first: it would take the processor and the memory (--force: even so).
 */
#define _GNU_SOURCE
#include <arm_neon.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define W 1920
#define H 1080
#define FRAME_BYTES ((size_t)W * H * 4)
#define MAX_REPS 64
#define MAX_STRIP 64

static uint32_t *srcA, *srcB, *over, *target, *scratch[2];
static uint8_t* screen;
static size_t pitch;
static uint8_t *fbMap, *fbSaved;
static size_t fbSize;

/* --- ES's NEON spans (es-core/src/renderers/soft/SoftRaster.cpp, spanNeon/finish8) ----------------- */

static inline uint8x8_t div255n(uint16x8_t x) { return vrshrn_n_u16(vaddq_u16(x, vrshrq_n_u16(x, 8)), 8); }
static inline uint8x8_t div255xn(uint16x8_t x) { return vrshrn_n_u16(vaddq_u16(x, vshrq_n_u16(x, 8)), 8); }

/* 8 source pixels over the 8 at dst (blendNormal) */
static inline void blend8(uint32_t* dst, uint8x8x4_t s)
{
	uint8x8x4_t d = vld4_u8((const uint8_t*)dst);
	const uint8x8_t a = s.val[3], ia = vmvn_u8(a);
	d.val[0] = div255xn(vmlal_u8(vmull_u8(s.val[0], a), d.val[0], ia));
	d.val[1] = div255xn(vmlal_u8(vmull_u8(s.val[1], a), d.val[1], ia));
	d.val[2] = div255xn(vmlal_u8(vmull_u8(s.val[2], a), d.val[2], ia));
	d.val[3] = div255xn(vmlal_u8(vmull_u8(s.val[3], a), d.val[3], ia));
	vst4_u8((uint8_t*)dst, d);
}

/* a picture with its own alpha, 1:1 (spanNeon<TEX_DIRECT, MOD_NONE>); pld: pixels ahead, 0 none */
static void overRow(uint32_t* dst, const uint32_t* src, int n, int pld)
{
	for (int i = 0; i < n; i += 8)
	{
		if (pld)
		{
			__builtin_prefetch(src + i + pld, 0);
			__builtin_prefetch(dst + i + pld, 1);
		}
		blend8(dst + i, vld4_u8((const uint8_t*)(src + i)));
	}
}

/* a picture faded: the colour multiplied in, every channel (spanNeon<TEX_DIRECT, MOD_CONST>) */
static void fadeRow(uint32_t* dst, const uint32_t* src, int n, uint32_t color, int pld)
{
	const uint8x8_t c0 = vdup_n_u8((uint8_t)color), c1 = vdup_n_u8((uint8_t)(color >> 8));
	const uint8x8_t c2 = vdup_n_u8((uint8_t)(color >> 16)), c3 = vdup_n_u8((uint8_t)(color >> 24));
	for (int i = 0; i < n; i += 8)
	{
		if (pld)
		{
			__builtin_prefetch(src + i + pld, 0);
			__builtin_prefetch(dst + i + pld, 1);
		}
		uint8x8x4_t s = vld4_u8((const uint8_t*)(src + i));
		s.val[0] = div255n(vmull_u8(s.val[0], c0));
		s.val[1] = div255n(vmull_u8(s.val[1], c1));
		s.val[2] = div255n(vmull_u8(s.val[2], c2));
		s.val[3] = div255n(vmull_u8(s.val[3], c3));
		blend8(dst + i, s);
	}
}

/* the same for a white colour (a fade's): only the alpha multiplied, x * 255 / 255 being x */
static void fadeAlphaRow(uint32_t* dst, const uint32_t* src, int n, uint32_t color, int pld)
{
	const uint8x8_t c3 = vdup_n_u8((uint8_t)(color >> 24));
	for (int i = 0; i < n; i += 8)
	{
		if (pld)
		{
			__builtin_prefetch(src + i + pld, 0);
			__builtin_prefetch(dst + i + pld, 1);
		}
		uint8x8x4_t s = vld4_u8((const uint8_t*)(src + i));
		s.val[3] = div255n(vmull_u8(s.val[3], c3));
		blend8(dst + i, s);
	}
}

/* SoftOutput.cpp's copyBytes, and ahead with PLD */
static void copyBytes(uint8_t* to, const uint8_t* from, size_t n, int pldBytes)
{
	size_t i = 0;
	for (; i + 64 <= n; i += 64)
	{
		if (pldBytes)
			__builtin_prefetch(from + i + pldBytes, 0);
		const uint32x4_t a = vld1q_u32((const uint32_t*)(from + i)), b = vld1q_u32((const uint32_t*)(from + i + 16));
		const uint32x4_t c = vld1q_u32((const uint32_t*)(from + i + 32)), d = vld1q_u32((const uint32_t*)(from + i + 48));
		vst1q_u32((uint32_t*)(to + i), a);
		vst1q_u32((uint32_t*)(to + i + 16), b);
		vst1q_u32((uint32_t*)(to + i + 32), c);
		vst1q_u32((uint32_t*)(to + i + 48), d);
	}
	memcpy(to + i, from + i, n - i);
}

/* --- two threads on a frame: this one the top half, a worker the bottom half --------------------------- */

typedef void (*Job)(int y0, int y1, int thread);
static Job job;
static pthread_barrier_t started, finished;

static void* worker(void* unused)
{
	(void)unused;
	for (;;)
	{
		pthread_barrier_wait(&started);
		job(H / 2, H, 1);
		pthread_barrier_wait(&finished);
	}
	return NULL;
}

static double nowMs(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static double runOnce(Job j, int threads)
{
	job = j;
	const double t0 = nowMs();
	if (threads == 1)
		j(0, H, 0);
	else
	{
		pthread_barrier_wait(&started);
		j(0, H / 2, 0);
		pthread_barrier_wait(&finished);
	}
	return nowMs() - t0;
}

static int reps = 9;

static int byTime(const void* a, const void* b)
{
	const double x = *(const double*)a, y = *(const double*)b;
	return x < y ? -1 : x > y;
}

/* median and best of reps, after one to warm up */
static double measure(Job j, int threads, double* best)
{
	double t[MAX_REPS];
	runOnce(j, threads);
	for (int i = 0; i < reps; i++)
		t[i] = runOnce(j, threads);
	qsort(t, reps, sizeof t[0], byTime);
	*best = t[0];
	return t[reps / 2];
}

/* --- what the jobs draw, set before each measure ------------------------------------------------------ */

static int pld;       /* pixels ahead (0: none) */
static int strip;     /* rows a strip */
static int alphaOnly; /* fadeAlphaRow for the fades */
static volatile uint32_t sink;

static void jobRead(int y0, int y1, int thread)
{
	(void)thread;
	const uint32_t* p = srcA + (size_t)y0 * W;
	const size_t n = (size_t)(y1 - y0) * W;
	uint32x4_t s0 = vdupq_n_u32(0), s1 = s0, s2 = s0, s3 = s0;
	for (size_t i = 0; i < n; i += 16)
	{
		if (pld)
			__builtin_prefetch(p + i + pld, 0);
		s0 = vaddq_u32(s0, vld1q_u32(p + i));
		s1 = vaddq_u32(s1, vld1q_u32(p + i + 4));
		s2 = vaddq_u32(s2, vld1q_u32(p + i + 8));
		s3 = vaddq_u32(s3, vld1q_u32(p + i + 12));
	}
	const uint32x4_t s = vaddq_u32(vaddq_u32(s0, s1), vaddq_u32(s2, s3));
	sink += vgetq_lane_u32(s, 0) + vgetq_lane_u32(s, 1) + vgetq_lane_u32(s, 2) + vgetq_lane_u32(s, 3);
}

static void jobWrite(int y0, int y1, int thread)
{
	(void)thread;
	memset(target + (size_t)y0 * W, 0, (size_t)(y1 - y0) * W * 4);
}

static void jobMemcpy(int y0, int y1, int thread)
{
	(void)thread;
	memcpy(target + (size_t)y0 * W, srcA + (size_t)y0 * W, (size_t)(y1 - y0) * W * 4);
}

static void jobNeonCopy(int y0, int y1, int thread)
{
	(void)thread;
	copyBytes((uint8_t*)(target + (size_t)y0 * W), (const uint8_t*)(srcA + (size_t)y0 * W), (size_t)(y1 - y0) * W * 4, pld * 4);
}

static void jobOver(int y0, int y1, int thread)
{
	(void)thread;
	for (int y = y0; y < y1; y++)
		overRow(target + (size_t)y * W, over + (size_t)y * W, W, pld);
}

static void jobFade(int y0, int y1, int thread)
{
	(void)thread;
	for (int y = y0; y < y1; y++)
		fadeRow(target + (size_t)y * W, srcA + (size_t)y * W, W, 0xDBFFFFFF, pld);
}

static void jobFadeAlpha(int y0, int y1, int thread)
{
	(void)thread;
	for (int y = y0; y < y1; y++)
		fadeAlphaRow(target + (size_t)y * W, srcA + (size_t)y * W, W, 0xDBFFFFFF, pld);
}

/* layer L of the frame's rows [y, y + rows), into dst (rows apart by W) */
static void layer(uint32_t* dst, int y, int rows, int L)
{
	for (int r = 0; r < rows; r++)
	{
		uint32_t* d = dst + (size_t)r * W;
		const size_t at = (size_t)(y + r) * W;
		switch (L)
		{
		case 0: memset(d, 0, W * 4); break;
		case 1: (alphaOnly ? fadeAlphaRow : fadeRow)(d, srcA + at, W, 0xDBFFFFFF, pld); break;
		case 2: (alphaOnly ? fadeAlphaRow : fadeRow)(d, srcB + at, W, 0x23FFFFFF, pld); break;
		default: overRow(d, over + at, W, pld); break;
		}
	}
}

/* the frame's rows [y, y + rows), from src (rows apart by W), copied to the screen */
static void present(const uint32_t* src, int y, int rows)
{
	for (int r = 0; r < rows; r++)
		copyBytes(screen + (size_t)(y + r) * pitch, (const uint8_t*)(src + (size_t)r * W), W * 4, pld * 4);
}

#define LAYERS 4

static void jobFrameLayers(int y0, int y1, int thread)
{
	(void)thread;
	for (int L = 0; L < LAYERS; L++)
		layer(target + (size_t)y0 * W, y0, y1 - y0, L);
	present(target + (size_t)y0 * W, y0, y1 - y0);
}

static void jobFrameStrips(int y0, int y1, int thread)
{
	(void)thread;
	for (int y = y0; y < y1; y += strip)
	{
		const int rows = y + strip < y1 ? strip : y1 - y;
		for (int L = 0; L < LAYERS; L++)
			layer(target + (size_t)y * W, y, rows, L);
	}
	present(target + (size_t)y0 * W, y0, y1 - y0);
}

static void jobFrameStripsShown(int y0, int y1, int thread)
{
	(void)thread;
	for (int y = y0; y < y1; y += strip)
	{
		const int rows = y + strip < y1 ? strip : y1 - y;
		for (int L = 0; L < LAYERS; L++)
			layer(target + (size_t)y * W, y, rows, L);
		present(target + (size_t)y * W, y, rows);
	}
}

static void jobFrameScratch(int y0, int y1, int thread)
{
	for (int y = y0; y < y1; y += strip)
	{
		const int rows = y + strip < y1 ? strip : y1 - y;
		for (int L = 0; L < LAYERS; L++)
			layer(scratch[thread], y, rows, L);
		present(scratch[thread], y, rows);
	}
}

static void jobPresent(int y0, int y1, int thread)
{
	(void)thread;
	present(target + (size_t)y0 * W, y0, y1 - y0);
}

static void jobPresentMemcpy(int y0, int y1, int thread)
{
	(void)thread;
	for (int y = y0; y < y1; y++)
		memcpy(screen + (size_t)y * pitch, target + (size_t)y * W, W * 4);
}

/* --- setup ---------------------------------------------------------------------------------------------- */

static void putBackScreen(void)
{
	if (fbMap && fbSaved)
		memcpy(fbMap, fbSaved, fbSize);
}

static void onSignal(int sig)
{
	putBackScreen();
	signal(sig, SIG_DFL);
	raise(sig);
}

static void* frameAlloc(size_t bytes)
{
	void* p = NULL;
	if (posix_memalign(&p, 64, bytes) != 0)
	{
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(p, 0, bytes);
	return p;
}

static int esRunning(void)
{
	DIR* proc = opendir("/proc");
	if (!proc)
		return 0;
	struct dirent* e;
	int found = 0;
	while (!found && (e = readdir(proc)) != NULL)
	{
		if (e->d_name[0] < '0' || e->d_name[0] > '9')
			continue;
		char path[64], comm[32] = "";
		snprintf(path, sizeof path, "/proc/%s/comm", e->d_name);
		FILE* f = fopen(path, "r");
		if (!f)
			continue;
		if (fgets(comm, sizeof comm, f) && strncmp(comm, "emulationstatio", 15) == 0)
			found = 1;
		fclose(f);
	}
	closedir(proc);
	return found;
}

static void printFile(const char* label, const char* path)
{
	char line[256] = "";
	FILE* f = fopen(path, "r");
	if (!f)
		return;
	if (fgets(line, sizeof line, f))
		printf("%s: %s", label, line);
	fclose(f);
}

static const char* openScreen(int useMem)
{
	static char what[128];
	if (!useMem)
	{
		int fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
		struct fb_var_screeninfo var;
		struct fb_fix_screeninfo fix;
		if (fd >= 0 && ioctl(fd, FBIOGET_VSCREENINFO, &var) == 0 && ioctl(fd, FBIOGET_FSCREENINFO, &fix) == 0)
		{
			size_t size = (size_t)fix.line_length * var.yres;
			if (fix.smem_len > 0 && size > fix.smem_len)
				size = fix.smem_len;
			if (var.bits_per_pixel == 32 && var.xres >= W && var.yres >= H && size >= (size_t)fix.line_length * H)
			{
				void* map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
				if (map != MAP_FAILED)
				{
					fbMap = map;
					fbSize = size;
					fbSaved = malloc(size);
					if (fbSaved)
					{
						memcpy(fbSaved, fbMap, size);
						atexit(putBackScreen);
						signal(SIGINT, onSignal);
						signal(SIGTERM, onSignal);
						signal(SIGHUP, onSignal);
						screen = fbMap;
						pitch = fix.line_length;
						snprintf(what, sizeof what, "/dev/fb0 %ux%u 32 bpp (its pixels put back at the end)", var.xres, var.yres);
						return what;
					}
					munmap(map, size);
					fbMap = NULL;
				}
			}
			snprintf(what, sizeof what, "memory (/dev/fb0 is %ux%u %u bpp, not 1920x1080 32 bpp: run it right after closing ES)",
				var.xres, var.yres, var.bits_per_pixel);
		}
		else
			snprintf(what, sizeof what, "memory (/dev/fb0: %s)", strerror(errno));
		if (fd >= 0)
			close(fd);
	}
	else
		snprintf(what, sizeof what, "memory (--mem)");
	screen = frameAlloc(FRAME_BYTES);
	pitch = W * 4;
	return what;
}

static void line(const char* name, Job j)
{
	double b1, b2;
	const double m1 = measure(j, 1, &b1);
	const double m2 = measure(j, 2, &b2);
	printf("  %-44s %7.1f ms (best %6.1f)  %7.1f ms (best %6.1f)\n", name, m1, b1, m2, b2);
}

static void lineRate(const char* name, Job j, double megabytes)
{
	double b1, b2;
	const double m1 = measure(j, 1, &b1);
	const double m2 = measure(j, 2, &b2);
	printf("  %-30s %7.1f ms %5.0f MB/s  %7.1f ms %5.0f MB/s\n", name, m1, megabytes / m1 * 1000, m2, megabytes / m2 * 1000);
}

int main(int argc, char** argv)
{
	int useMem = 0, force = 0;
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "--reps") && i + 1 < argc)
			reps = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--mem"))
			useMem = 1;
		else if (!strcmp(argv[i], "--force"))
			force = 1;
		else
		{
			fprintf(stderr, "usage: %s [--reps N] [--mem] [--force]\n", argv[0]);
			return 2;
		}
	}
	if (reps < 3) reps = 3;
	if (reps > MAX_REPS) reps = MAX_REPS;
	if (esRunning() && !force)
	{
		fprintf(stderr, "EmulationStation is running: close it first (it takes the processor and the memory), or --force\n");
		return 1;
	}

	setvbuf(stdout, NULL, _IOLBF, 0);
	printf("es-mister-bench: a 1920x1080 frame, %d repetitions, median (best)\n", reps);
	printFile("load", "/proc/loadavg");
	printFile("cpu0 kHz", "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
	printf("processors: %ld\n", sysconf(_SC_NPROCESSORS_ONLN));

	srcA = frameAlloc(FRAME_BYTES);
	srcB = frameAlloc(FRAME_BYTES);
	over = frameAlloc(FRAME_BYTES);
	target = frameAlloc(FRAME_BYTES);
	scratch[0] = frameAlloc((size_t)W * MAX_STRIP * 4);
	scratch[1] = frameAlloc((size_t)W * MAX_STRIP * 4);
	uint32_t seed = 12345;
	for (size_t i = 0; i < (size_t)W * H; i++)
	{
		seed = seed * 1664525u + 1013904223u;
		srcA[i] = 0xFF000000u | (seed >> 8);
		srcB[i] = 0xFF000000u | (seed * 2654435761u >> 8);
		/* a frame's art: clear, solid and edges in between */
		const uint32_t a = (i / 7) % 3 == 0 ? 0 : ((i / 7) % 3 == 1 ? 255 : (seed >> 24));
		over[i] = (a << 24) | (seed & 0x00FFFFFF);
	}
	printf("screen: %s\n", openScreen(useMem));

	pthread_t thread;
	pthread_barrier_init(&started, NULL, 2);
	pthread_barrier_init(&finished, NULL, 2);
	if (pthread_create(&thread, NULL, worker, NULL) != 0)
	{
		fprintf(stderr, "cannot start a thread\n");
		return 1;
	}

	/* the alpha-only fade draws the same pixels as the fade: every alpha, over pictures and their alpha */
	{
		uint32_t *a = frameAlloc((size_t)W * 4 * 64), *b = frameAlloc((size_t)W * 4 * 64);
		memcpy(a, over, (size_t)W * 4 * 64);
		memcpy(b, over, (size_t)W * 4 * 64);
		for (uint32_t alpha = 0; alpha < 256; alpha++)
		{
			const uint32_t c = (alpha << 24) | 0x00FFFFFF;
			const size_t row = (size_t)(alpha % 64) * W, from = (size_t)alpha * W;
			fadeRow(a + row, srcA + from, W, c, 0);
			fadeAlphaRow(b + row, srcA + from, W, c, 0);
			fadeRow(a + row, over + from, W, c, 0);
			fadeAlphaRow(b + row, over + from, W, c, 0);
		}
		printf("alpha-only fade, the same pixels as the fade: %s\n", memcmp(a, b, (size_t)W * 4 * 64) == 0 ? "yes" : "NO");
		free(a);
		free(b);
	}

	const double mb = FRAME_BYTES / 1e6;
	printf("\n1. memory, a frame (%.1f MB)              1 thread             2 threads\n", mb);
	pld = 0;   lineRate("read", jobRead, mb);
	pld = 64;  lineRate("read, PLD 256 B", jobRead, mb);
	pld = 0;   lineRate("write (memset)", jobWrite, mb);
	pld = 0;   lineRate("copy (memcpy)", jobMemcpy, mb);
	pld = 0;   lineRate("copy (NEON, ES's)", jobNeonCopy, mb);
	pld = 64;  lineRate("copy (NEON, PLD 256 B)", jobNeonCopy, mb);

	static const int plds[] = { 0, 16, 32, 64, 128, 256 }; /* pixels: 0, 64, 128, 256, 512, 1024 bytes */
	const int nplds = sizeof plds / sizeof plds[0];
	printf("\n2. one layer over the frame                       1 thread                  2 threads\n");
	int bestPld = 0;
	double bestTime = 1e9;
	for (int i = 0; i < nplds; i++)
	{
		char name[64];
		pld = plds[i];
		snprintf(name, sizeof name, "fade (as ES), PLD %d B", pld * 4);
		double b1, b2;
		const double m1 = measure(jobFade, 1, &b1);
		const double m2 = measure(jobFade, 2, &b2);
		printf("  %-44s %7.1f ms (best %6.1f)  %7.1f ms (best %6.1f)\n", name, m1, b1, m2, b2);
		if (m2 < bestTime)
		{
			bestTime = m2;
			bestPld = pld;
		}
	}
	for (int i = 0; i < (bestPld ? 2 : 1); i++)
	{
		char name[64];
		pld = i == 0 ? 0 : bestPld;
		snprintf(name, sizeof name, "fade, alpha only, PLD %d B", pld * 4);
		line(name, jobFadeAlpha);
		snprintf(name, sizeof name, "picture with its alpha, PLD %d B", pld * 4);
		line(name, jobOver);
	}
	printf("  PLD for the frames: %d B (the fastest fade on 2 threads)\n", bestPld * 4);

	printf("\n3. a carousel frame: clear, 2 fades, a picture, copy  1 thread                  2 threads\n");
	static const int strips[] = { 8, 16, 32 };
	for (int p = 0; p < (bestPld ? 2 : 1); p++)
	{
		pld = p == 0 ? 0 : bestPld;
		alphaOnly = 0;
		char name[80];
		snprintf(name, sizeof name, "layer by layer (pkg 16), PLD %d B", pld * 4);
		line(name, jobFrameLayers);
		for (int s = 0; s < 3; s++)
		{
			strip = strips[s];
			snprintf(name, sizeof name, "strips of %d (pkg 17), PLD %d B", strip, pld * 4);
			line(name, jobFrameStrips);
		}
		for (int s = 0; s < 3; s++)
		{
			strip = strips[s];
			snprintf(name, sizeof name, "strips of %d shown at once, PLD %d B", strip, pld * 4);
			line(name, jobFrameStripsShown);
		}
		for (int s = 0; s < 3; s++)
		{
			strip = strips[s];
			snprintf(name, sizeof name, "strips of %d apart, shown, PLD %d B", strip, pld * 4);
			line(name, jobFrameScratch);
		}
	}
	pld = bestPld;
	alphaOnly = 1;
	for (int s = 0; s < 3; s++)
	{
		char name[80];
		strip = strips[s];
		snprintf(name, sizeof name, "strips of %d apart, alpha-only, PLD %d B", strip, pld * 4);
		line(name, jobFrameScratch);
	}
	{
		char name[80];
		snprintf(name, sizeof name, "layer by layer, alpha-only, PLD %d B", pld * 4);
		line(name, jobFrameLayers);
	}
	alphaOnly = 0;

	printf("\n4. the copy to the screen                           1 thread              2 threads\n");
	pld = 0;       lineRate("memcpy", jobPresentMemcpy, mb);
	pld = 0;       lineRate("NEON (ES's)", jobPresent, mb);
	pld = bestPld; lineRate("NEON, PLD", jobPresent, mb);
	pld = 64;      lineRate("NEON, PLD 256 B", jobPresent, mb);

	printf("\ndone\n");
	return 0;
}
