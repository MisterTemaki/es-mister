#pragma once
#ifndef ES_CORE_RENDERERS_SOFT_CAPTURE_H
#define ES_CORE_RENDERERS_SOFT_CAPTURE_H

// A frame of the software renderer kept in a file: its draw commands, their vertices and state, and the
// textures they read, as the rasterizer got them. es-mister-replay (mister/tests) draws captured frames
// again, to time the rasterizer on the same frames before and after a change (on the MiSTer itself) and
// to check that a change leaves every pixel as it was. Renderer_Soft writes them: the test script's
// "capture NAME", and with ES_SOFT_SLOW_CAPTURE=1 the slow frames it dumps (ES_SOFT_SLOW_DUMP).

#include "SoftRaster.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Renderer
{
	namespace Soft
	{
		struct CapturedCommand
		{
			bool stencil = false;      // a stencil shape, not a draw
			Topology topology = Topology::Strip;
			DrawState state;           // its texture is textures[texture]
			int texture = -1;
			std::vector<SVertex> vertices;
		};

		struct Capture
		{
			int width = 0, height = 0;
			std::vector<std::shared_ptr<Texture>> textures;
			std::vector<CapturedCommand> commands;
		};

		bool saveCapture(const std::string& path, const Capture& capture);
		bool loadCapture(const std::string& path, Capture& capture, std::string& error);

		// The frame drawn into owner (resized to it) as the renderer draws a whole frame: in bands of
		// rows (one a thread), each a few rows at a time (strip; 0: the band at once) through every command,
		// each part cleared first and every command clipped to it
		// (oldStencil: every stencil shape set in every part, as the renderer did before 15/11/2026, to compare)
		void drawCapture(const Capture& capture, Raster& owner, int threads, int strip, bool oldStencil = false);
	}
}

#endif
