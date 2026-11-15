// es-mister-replay: frames of the software renderer captured to files (SoftCapture.h) drawn again, timed,
// with a hash of their pixels. On the MiSTer it times the rasterizer on real frames, before and after a
// change, with nothing else moving; on any machine, the same hash before and after a change means the
// change left every pixel as it was.
//
//   es-mister-replay [-t THREADS] [-s STRIP] [-r REPEATS] [--ppm DIR] FRAME.escf...
//
//   -t  threads, one a band of rows (default 2, the DE10-Nano's cores)
//   -s  rows drawn at a time through all the commands (default as ES: by the width, 24 at 1080p; 0: a band
//       at once)
//   -r  times each frame is drawn (default 9): the first (pictures scaled for the first time) and the
//       median and best of the others are given
//   --ppm  each frame also written as DIR/NAME.ppm, to look at
//   --old-stencil  every stencil shape set in every part, as the renderer did before 15/11/2026
//   --commands  each command's time too, drawn alone on one thread over the frame as drawn up to it
//       (the costliest first), with what it drew: where a frame's time goes
//
// The time is the rasterizer's alone: no copy to the screen, nothing of ES's.
#include "renderers/soft/SoftCapture.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace Renderer::Soft;

namespace
{
	uint64_t hashPixels(const uint32_t* pixels, size_t count)
	{
		uint64_t hash = 1469598103934665603ULL;
		for (size_t i = 0; i < count; i++)
			hash = (hash ^ pixels[i]) * 1099511628211ULL;
		return hash;
	}

	bool writePpm(const std::string& path, const uint32_t* pixels, int width, int height)
	{
		FILE* f = fopen(path.c_str(), "wb");
		if (f == nullptr)
			return false;
		fprintf(f, "P6\n%d %d\n255\n", width, height);
		std::vector<uint8_t> row((size_t)width * 3);
		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < width; x++)
			{
				const uint32_t c = pixels[(size_t)y * width + x];
				row[(size_t)x * 3] = (uint8_t)(c >> 16);
				row[(size_t)x * 3 + 1] = (uint8_t)(c >> 8);
				row[(size_t)x * 3 + 2] = (uint8_t)c;
			}
			fwrite(row.data(), 1, row.size(), f);
		}
		return fclose(f) == 0;
	}

	// every command drawn alone, in order, on one raster (the frame as it builds up), timed; the costliest
	// listed first with the pixels it drew by kind
	void timeCommands(const Capture& capture)
	{
		static const char* kinds[PX_KINDS] = { "fill", "copy", "colour", "1:1", "nearest", "bilinear", "triangle" };
		Raster raster;
		raster.resize(capture.width, capture.height);
		raster.clear(0);
		struct Line { double ms; std::string text; };
		std::vector<Line> lines;
		double total = 0;
		for (size_t i = 0; i < capture.commands.size(); i++)
		{
			const CapturedCommand& c = capture.commands[i];
			raster.stats() = RasterStats();
			DrawState state = c.state;
			const auto start = std::chrono::steady_clock::now();
			if (c.stencil)
				raster.setStencil(c.vertices.data(), (int)c.vertices.size(), state);
			else
			{
				state.texture = c.texture >= 0 ? capture.textures[c.texture].get() : nullptr;
				raster.draw(c.vertices.data(), (int)c.vertices.size(), c.topology, state);
			}
			const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			total += ms;
			const Texture* t = state.texture;
			char text[256];
			snprintf(text, sizeof text, "%4zu %s %4zu vertices, texture %s, blend %d/%d%s, colour %08x:", i,
				c.stencil ? "stencil" : (c.topology == Topology::Strip ? "strip" : (c.topology == Topology::Fan ? "fan" : "lines")),
				c.vertices.size(), t ? (std::to_string(t->width) + "x" + std::to_string(t->height) + (t->opaque ? " opaque" : "") + (t->linear ? "" : " nearest")).c_str() : "-",
				c.state.srcBlend, c.state.dstBlend, c.state.stencil != StencilTest::Off ? " stencil-tested" : "", c.vertices.empty() ? 0u : c.vertices[0].color);
			std::string line = text;
			for (int k = 0; k < PX_KINDS; k++)
				if (raster.stats().kinds[k])
					line += std::string(" ") + kinds[k] + " " + std::to_string(raster.stats().kinds[k] / 1000) + "k";
			lines.push_back({ ms, line });
		}
		std::sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.ms > b.ms; });
		for (size_t i = 0; i < lines.size() && i < 25; i++)
			printf("    %7.3f ms %s\n", lines[i].ms, lines[i].text.c_str());
		printf("    %7.3f ms all %zu commands on one thread\n", total, capture.commands.size());
	}

	std::string baseName(const std::string& path)
	{
		const size_t slash = path.find_last_of('/');
		std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
		const size_t dot = name.rfind('.');
		return dot == std::string::npos ? name : name.substr(0, dot);
	}
}

int main(int argc, char** argv)
{
	int threads = 2, strip = -1, repeats = 9;
	bool oldStencil = false;
	std::string ppm;
	bool commands = false;
	std::vector<std::string> files;
	for (int i = 1; i < argc; i++)
	{
		const std::string arg = argv[i];
		if (arg == "-t" && i + 1 < argc)
			threads = atoi(argv[++i]);
		else if (arg == "-s" && i + 1 < argc)
			strip = atoi(argv[++i]);
		else if (arg == "-r" && i + 1 < argc)
			repeats = atoi(argv[++i]);
		else if (arg == "--ppm" && i + 1 < argc)
			ppm = argv[++i];
		else if (arg == "--commands")
			commands = true;
		else if (arg == "--old-stencil")
			oldStencil = true;
		else if (!arg.empty() && arg[0] == '-')
		{
			fprintf(stderr, "usage: %s [-t THREADS] [-s STRIP] [-r REPEATS] [--ppm DIR] FRAME.escf...\n", argv[0]);
			return 2;
		}
		else
			files.push_back(arg);
	}
	if (files.empty())
	{
		fprintf(stderr, "usage: %s [-t THREADS] [-s STRIP] [-r REPEATS] [--ppm DIR] FRAME.escf...\n", argv[0]);
		return 2;
	}
	repeats = std::max(1, std::min(1000, repeats));

	printf("es-mister-replay: %d threads, strips of %s rows, each frame %d times (ms: first, median, best of the rest)%s\n", threads,
		strip >= 0 ? std::to_string(strip).c_str() : "auto", repeats, oldStencil ? ", stencils in every part" : "");
	double total = 0;
	int failed = 0;
	for (const std::string& file : files)
	{
		Capture capture;
		std::string error;
		if (!loadCapture(file, capture, error))
		{
			fprintf(stderr, "%s\n", error.c_str());
			failed++;
			continue;
		}
		Raster owner;
		std::vector<double> times;
		for (int r = 0; r < repeats; r++)
		{
			const auto start = std::chrono::steady_clock::now();
			drawCapture(capture, owner, threads, strip >= 0 ? strip : std::max(8, std::min(64, 46080 / capture.width)), oldStencil);
			times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
		}
		const double first = times[0];
		std::vector<double> rest(times.begin() + (times.size() > 1 ? 1 : 0), times.end());
		std::sort(rest.begin(), rest.end());
		const double median = rest[rest.size() / 2];
		total += median;
		const uint64_t hash = hashPixels(owner.pixels(), (size_t)capture.width * capture.height);
		printf("%-28s %4zu commands %dx%d  %8.2f %8.2f %8.2f  pixels %016llx\n", baseName(file).c_str(), capture.commands.size(),
			capture.width, capture.height, first, median, rest[0], (unsigned long long)hash);
		if (commands)
			timeCommands(capture);
		if (!ppm.empty() && !writePpm(ppm + "/" + baseName(file) + ".ppm", owner.pixels(), capture.width, capture.height))
			fprintf(stderr, "cannot write %s/%s.ppm\n", ppm.c_str(), baseName(file).c_str());
	}
	printf("sum of the medians: %.2f ms\n", total);
	return failed == 0 ? 0 : 1;
}
