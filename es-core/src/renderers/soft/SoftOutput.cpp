#include "SoftOutput.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace Renderer
{
	namespace Soft
	{
		namespace
		{
			std::string env(const char* name, const char* fallback = "")
			{
				const char* value = getenv(name);
				return value != nullptr ? value : fallback;
			}

			// a row's bytes copied (64 bytes a step with NEON: the framebuffer's memory takes wide stores best),
			// the frame's next bytes asked for ahead (ES_SOFT_PLD, as the spans): a 1080p frame to the MiSTer's
			// screen in 16.3 ms instead of 20.3 (es-mister-bench)
			inline void copyBytes(uint8_t* to, const uint8_t* from, size_t n)
			{
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
				static const size_t ahead = []
				{
					const char* value = getenv("ES_SOFT_PLD");
					const int bytes = value != nullptr ? atoi(value) : 256;
					return (size_t)std::max(0, std::min(bytes, 4096));
				}();
				static const bool pairedPrefetch = []
				{
					const char* value = getenv("ES_SOFT_COPY_PLD_PAIR");
					const char* optimizations = getenv("ES_SOFT_OPTIMIZATIONS");
					if (optimizations && strcmp(optimizations, "0") == 0) return false;
					if (value) return strcmp(value, "1") == 0;
#ifdef MISTER
					return true;
#else
					return false;
#endif
				}();
				size_t i = 0;
				for (; i + 64 <= n; i += 64)
				{
					if (ahead)
					{
						__builtin_prefetch(from + i + ahead, 0);
						// The existing 64-byte block spans two Cortex-A9 cache lines.
						// Keep the configured lookahead and request its second line too.
						if (pairedPrefetch && n - i > ahead + 32)
							__builtin_prefetch(from + i + ahead + 32, 0);
					}
					const uint32x4_t a = vld1q_u32((const uint32_t*)(from + i)), b = vld1q_u32((const uint32_t*)(from + i + 16));
					const uint32x4_t c = vld1q_u32((const uint32_t*)(from + i + 32)), d = vld1q_u32((const uint32_t*)(from + i + 48));
					vst1q_u32((uint32_t*)(to + i), a);
					vst1q_u32((uint32_t*)(to + i + 16), b);
					vst1q_u32((uint32_t*)(to + i + 32), c);
					vst1q_u32((uint32_t*)(to + i + 48), d);
				}
				memcpy(to + i, from + i, n - i);
#else
				memcpy(to, from, n);
#endif
			}

			class NullOutput : public Output
			{
			public:
				bool open(int& width, int& height, std::string& error) override
				{
					width = 640;
					height = 360;
					const std::string size = env("ES_SOFT_SIZE");
					int w = 0, h = 0;
					if (!size.empty() && sscanf(size.c_str(), "%dx%d", &w, &h) == 2 && w > 0 && h > 0)
					{
						width = w;
						height = h;
					}
					mWidth = width;
					mHeight = height;
					return true;
				}

				int64_t presentRect(const uint32_t*, int width, int height, int x0, int y0, int x1, int y1) override
				{
					x0 = std::max(0, x0); y0 = std::max(0, y0);
					x1 = std::min(width, x1); y1 = std::min(height, y1);
					return x0 < x1 && y0 < y1 ? (int64_t)(x1 - x0) * (y1 - y0) : 0;
				}

				std::string describe() const override { return "memory " + std::to_string(mWidth) + "x" + std::to_string(mHeight); }

			private:
				int mWidth = 0, mHeight = 0;
			};

#if defined(__linux__)
			class FbdevOutput : public Output
			{
			public:
				explicit FbdevOutput(const std::string& path) : mPath(path) { }
				~FbdevOutput() override { close(); }

				bool open(int& width, int& height, std::string& error) override
				{
					mFd = ::open(mPath.c_str(), O_RDWR | O_CLOEXEC);
					if (mFd < 0)
					{
						error = "cannot open " + mPath + ": " + strerror(errno);
						return false;
					}

					fb_var_screeninfo var;
					fb_fix_screeninfo fix;
					if (ioctl(mFd, FBIOGET_VSCREENINFO, &var) < 0 || ioctl(mFd, FBIOGET_FSCREENINFO, &fix) < 0)
					{
						error = "cannot read the framebuffer's mode: " + std::string(strerror(errno));
						close();
						return false;
					}

					mWidth = var.xres;
					mHeight = var.yres;
					mBpp = var.bits_per_pixel;
					mPitch = fix.line_length;
					mRed = var.red; mGreen = var.green; mBlue = var.blue; mAlpha = var.transp;

					if (mBpp != 32 && mBpp != 16)
					{
						error = "unsupported framebuffer depth " + std::to_string(mBpp);
						close();
						return false;
					}

					mSize = (size_t)mPitch * mHeight;
					if (fix.smem_len > 0 && mSize > fix.smem_len)
						mSize = fix.smem_len;

					void* map = mmap(nullptr, mSize, PROT_READ | PROT_WRITE, MAP_SHARED, mFd, 0);
					if (map == MAP_FAILED)
					{
						error = "cannot map the framebuffer: " + std::string(strerror(errno));
						close();
						return false;
					}
					mMap = (uint8_t*)map;

					// the same as ours (0xAARRGGBB in a 32-bit word): rows are copied as they are
					mNative = mBpp == 32 && mRed.offset == 16 && mGreen.offset == 8 && mBlue.offset == 0 && mRed.length == 8 && mGreen.length == 8 && mBlue.length == 8;

					// the console stops drawing (its text and its cursor) over the frames
					const std::string tty = env("ES_SOFT_TTY", "/dev/tty0");
					if (tty != "none")
					{
						mTty = ::open(tty.c_str(), O_RDWR | O_CLOEXEC);
						if (mTty >= 0 && ioctl(mTty, KDSETMODE, KD_GRAPHICS) < 0)
						{
							::close(mTty);
							mTty = -1;
						}
					}

					width = mWidth;
					height = mHeight;
					return true;
				}

				void close() override
				{
					if (mMap != nullptr)
					{
						munmap(mMap, mSize);
						mMap = nullptr;
					}
					if (mFd >= 0)
					{
						::close(mFd);
						mFd = -1;
					}
					if (mTty >= 0)
					{
						ioctl(mTty, KDSETMODE, KD_TEXT);
						::close(mTty);
						mTty = -1;
					}
				}

				// Straight to the framebuffer: the renderer gives only what changed, so no copy of the screen is
				// kept to compare rows with (that read and wrote each row twice more)
				int64_t presentRect(const uint32_t* pixels, int width, int height, int x0, int y0, int x1, int y1) override
				{
					if (mMap == nullptr)
						return 0;
					x0 = std::max(0, x0); y0 = std::max(0, y0);
					x1 = std::min(std::min(width, mWidth), x1);
					y1 = std::min(std::min(height, mHeight), y1);
					if (x0 >= x1 || y0 >= y1)
						return 0;

					const int cols = x1 - x0;
					const int bytes = mBpp / 8;
					const size_t rowBytes = (size_t)cols * bytes;
					thread_local std::vector<uint8_t> converted; // each band's thread its own
					if (!mNative && converted.size() < rowBytes)
						converted.resize(rowBytes);

					for (int y = y0; y < y1; y++)
					{
						const uint32_t* src = pixels + (size_t)y * width + x0;
						const uint8_t* row = (const uint8_t*)src;
						if (!mNative)
						{
							convertRow(src, converted.data(), cols);
							row = converted.data();
						}
						copyBytes(mMap + (size_t)y * mPitch + (size_t)x0 * bytes, row, rowBytes);
					}
					return (int64_t)cols * (y1 - y0);
				}

				std::string describe() const override
				{
					return mPath + " " + std::to_string(mWidth) + "x" + std::to_string(mHeight) + " " + std::to_string(mBpp) + " bpp" + (mNative ? "" : " (converted)");
				}

			private:
				static uint32_t place(uint32_t value8, const fb_bitfield& field)
				{
					if (field.length == 0)
						return 0;
					return (value8 >> (8 - field.length)) << field.offset;
				}

				void convertRow(const uint32_t* src, uint8_t* dst, int count)
				{
					if (mBpp == 32)
					{
						uint32_t* out = (uint32_t*)dst;
						for (int i = 0; i < count; i++)
						{
							const uint32_t c = src[i];
							out[i] = place((c >> 16) & 0xFF, mRed) | place((c >> 8) & 0xFF, mGreen) | place(c & 0xFF, mBlue) | place(0xFF, mAlpha);
						}
					}
					else
					{
						uint16_t* out = (uint16_t*)dst;
						for (int i = 0; i < count; i++)
						{
							const uint32_t c = src[i];
							out[i] = (uint16_t)(place((c >> 16) & 0xFF, mRed) | place((c >> 8) & 0xFF, mGreen) | place(c & 0xFF, mBlue));
						}
					}
				}

				std::string mPath;
				int mFd = -1, mTty = -1;
				uint8_t* mMap = nullptr;
				size_t mSize = 0;
				int mWidth = 0, mHeight = 0, mBpp = 0, mPitch = 0;
				fb_bitfield mRed = {}, mGreen = {}, mBlue = {}, mAlpha = {};
				bool mNative = false;
			};
#endif
		}

		std::unique_ptr<Output> createOutput()
		{
			std::string kind = env("ES_SOFT_OUTPUT");
#if defined(__linux__)
			if (kind.empty())
				kind = access("/dev/fb0", F_OK) == 0 ? "fbdev" : "null";

			if (kind.compare(0, 5, "fbdev") == 0)
			{
				const std::string path = kind.size() > 6 && kind[5] == ':' ? kind.substr(6) : "/dev/fb0";
				return std::unique_ptr<Output>(new FbdevOutput(path));
			}
#endif
			return std::unique_ptr<Output>(new NullOutput());
		}
	}
}
