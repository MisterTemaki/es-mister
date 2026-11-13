#pragma once
#ifndef ES_CORE_RENDERER_SOFT_H
#define ES_CORE_RENDERER_SOFT_H

// The software renderer: ES drawn with no GPU, for the MiSTer FPGA (an ARM Cortex-A9 and the Linux
// framebuffer the FPGA scales to the screen). Every draw call of a frame is recorded, already in
// screen pixels, with a hash of the whole frame; when the frame is the same as the last one nothing is
// drawn or copied. Otherwise the frame is drawn by the rasterizer (soft/SoftRaster.h) and the rows
// that changed are copied to the output (soft/SoftOutput.h). It draws what OpenGL ES 1.0 draws, with
// the stencil for rounded corners and the saturation of images besides; no shaders.
//
//   ES_SOFT_FPS     the most frames a second when VSync is on (default 60; 0: no limit)
//   ES_SOFT_STATS   frames between the timing lines in the log (default 600; 0: none)
//   ES_SOFT_STATS_FILE  a file the timing lines are added to as well (ES logs them only with --debug)

#if defined(USE_SOFTWARE_RENDERER)

#define RENDERER_SOFTWARE

#include "Renderer.h"

namespace Renderer
{
	// The output's size, opening it if needed, for the window ES makes; false when it cannot open
	bool getSoftwareOutputSize(int& width, int& height);

	class SoftRenderer : public IRenderer
	{
	public:
		std::string getDriverName() override;
		std::vector<std::pair<std::string, std::string>> getDriverInformation() override;

		unsigned int getWindowFlags() override;
		void         setupWindow() override;

		void         createContext() override;
		void         destroyContext() override;

		void         resetCache() override;

		unsigned int createTexture(const Texture::Type _type, const bool _linear, const bool _repeat, const unsigned int _width, const unsigned int _height, void* _data) override;
		void         destroyTexture(const unsigned int _texture) override;
		void         updateTexture(const unsigned int _texture, const Texture::Type _type, const unsigned int _x, const unsigned _y, const unsigned int _width, const unsigned int _height, void* _data) override;
		void         bindTexture(const unsigned int _texture) override;

		void         drawLines(const Vertex* _vertices, const unsigned int _numVertices, const Blend::Factor _srcBlendFactor = Blend::SRC_ALPHA, const Blend::Factor _dstBlendFactor = Blend::ONE_MINUS_SRC_ALPHA) override;
		void         drawTriangleStrips(const Vertex* _vertices, const unsigned int _numVertices, const Blend::Factor _srcBlendFactor = Blend::SRC_ALPHA, const Blend::Factor _dstBlendFactor = Blend::ONE_MINUS_SRC_ALPHA, bool verticesChanged = true) override;
		void         drawTriangleFan(const Vertex* _vertices, const unsigned int _numVertices, const Blend::Factor _srcBlendFactor = Blend::SRC_ALPHA, const Blend::Factor _dstBlendFactor = Blend::ONE_MINUS_SRC_ALPHA) override;
		void         drawSolidRectangle(const float _x, const float _y, const float _w, const float _h, const unsigned int _fillColor, const unsigned int _borderColor, float borderWidth = 1, float cornerRadius = 0) override;

		void         setProjection(const Transform4x4f& _projection) override;
		void         setMatrix(const Transform4x4f& _matrix) override;
		void         setViewport(const Rect& _viewport) override;
		void         setScissor(const Rect& _scissor) override;

		void         setStencil(const Vertex* _vertices, const unsigned int _numVertices) override;
		void         disableStencil() override;

		void         setSwapInterval() override;
		void         swapBuffers() override;

		size_t       getTotalMemUsage() override;
	};
}

#endif // USE_SOFTWARE_RENDERER

#endif
