#include "SoftOutput.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <atomic>
#include <thread>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
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

			// how far ahead the frame's rows are asked for (PLD) while copied, in bytes (ES_SOFT_PLD, default 256;
			// 0: not): on the DE10-Nano a whole frame's copy 1.25 times faster (es-mister-bench)
			size_t aheadBytes()
			{
				static const size_t ahead = []
				{
					const char* value = getenv("ES_SOFT_PLD");
					return (size_t)std::max(0, std::min(4096, value != nullptr ? atoi(value) : 256));
				}();
				return ahead;
			}

			// a row's bytes copied (64 bytes a step with NEON: the framebuffer's memory takes wide stores best)
			inline void copyBytes(uint8_t* to, const uint8_t* from, size_t n)
			{
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
				const size_t ahead = aheadBytes();
				size_t i = 0;
				for (; i + 64 <= n; i += 64)
				{
					if (ahead)
						__builtin_prefetch(from + i + ahead, 0);
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
			// The console ES draws on, held the way graphics programs hold one (VT_PROCESS): the kernel asks
			// before switching away from it (releaseSignal) and says when it is back (acquireSignal); a thread
			// of its own answers at once, whatever ES's thread is doing (loading a list, waiting for a game).
			// Asked to let go, the frames stop going to the screen (presenting() false: the console switched
			// to writes there), and the switch is let through once no frame is being copied.
			//
			// On the MiSTer (ES_SOFT_MISTER_MODE=1) it also keeps the framebuffer's mode at ES's size: Main_MiSTer
			// shows the Linux framebuffer at its console's size when F9 brings it back (after F12, or after a
			// script), and ES's rows were shown as rows of another width.
			class Console
			{
			public:
				// the signal handler's end of the pipe (one console at a time)
				static std::atomic<int>& pipeOut()
				{
					static std::atomic<int> fd{ -1 };
					return fd;
				}

				static int releaseSignal() { return SIGRTMIN + 6; }
				static int acquireSignal() { return SIGRTMIN + 7; }

				// Puts the active console in graphics mode and holds it; false when there is none to hold
				bool open(int width, int height, bool misterMode)
				{
					mWidth = width;
					mHeight = height;
					mMisterMode = misterMode;

					const std::string tty = env("ES_SOFT_TTY", "");
					if (tty == "none")
						return false;
					if (!tty.empty())
						mFd = ::open(tty.c_str(), O_RDWR | O_CLOEXEC);
					else
					{
						// the console active now (ES's): by its number, so the switches are about it
						int any = ::open("/dev/tty0", O_RDWR | O_CLOEXEC);
						if (any < 0)
							return false;
						vt_stat state = {};
						if (ioctl(any, VT_GETSTATE, &state) == 0 && state.v_active > 0)
						{
							mVt = state.v_active;
							mFd = ::open(("/dev/tty" + std::to_string(mVt)).c_str(), O_RDWR | O_CLOEXEC);
						}
						if (mFd < 0)
							mFd = any;
						else
							::close(any);
					}
					if (mFd < 0)
						return false;
					if (ioctl(mFd, KDSETMODE, KD_GRAPHICS) < 0)
					{
						::close(mFd);
						mFd = -1;
						return false;
					}

					// the switches asked of us, from the signals to the thread through a pipe
					int fds[2];
					if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) == 0)
					{
						mPipeIn = fds[0];
						pipeOut().store(fds[1]);
						struct sigaction action = {};
						action.sa_handler = &Console::onSignal;
						action.sa_flags = SA_RESTART;
						sigemptyset(&action.sa_mask);
						sigaction(releaseSignal(), &action, nullptr);
						sigaction(acquireSignal(), &action, nullptr);
						vt_mode mode = {};
						mode.mode = VT_PROCESS;
						mode.relsig = (short)releaseSignal();
						mode.acqsig = (short)acquireSignal();
						mProcess = ioctl(mFd, VT_SETMODE, &mode) == 0;
					}
					mPresenting = true;
					mStop = false;
					if (mProcess || mMisterMode)
					{
						// the signals go to the thread below (it lets them through): not to this thread nor
						// to those it makes from now on, whose waits they would cut short
						sigset_t set;
						sigemptyset(&set);
						sigaddset(&set, releaseSignal());
						sigaddset(&set, acquireSignal());
						pthread_sigmask(SIG_BLOCK, &set, nullptr);
						mThread = std::thread([this] { run(); });
					}
					return true;
				}

				void close()
				{
					if (mThread.joinable())
					{
						mStop = true;
						const int out = pipeOut().load();
						if (out >= 0)
						{
							const char q = 'q';
							if (write(out, &q, 1) < 0) { }
						}
						mThread.join();
					}
					if (mFd >= 0)
					{
						if (mProcess)
						{
							vt_mode mode = {};
							mode.mode = VT_AUTO;
							ioctl(mFd, VT_SETMODE, &mode);
							mProcess = false;
						}
						ioctl(mFd, KDSETMODE, KD_TEXT);
						::close(mFd);
						mFd = -1;
					}
					const int out = pipeOut().exchange(-1);
					if (out >= 0)
						::close(out);
					if (mPipeIn >= 0)
					{
						::close(mPipeIn);
						mPipeIn = -1;
					}
					mPresenting = true;
				}

				// A frame's rows may go to the screen: true, and counted until done() (false: not now, nothing
				// to copy)
				bool begin()
				{
					mCopying.fetch_add(1);
					if (mPresenting.load())
						return true;
					mCopying.fetch_sub(1);
					return false;
				}
				void done() { mCopying.fetch_sub(1); }

				bool takeRedraw() { return mRedraw.exchange(false); }

			private:
				static void onSignal(int sig)
				{
					const int saved = errno;
					const int out = pipeOut().load();
					if (out >= 0)
					{
						const char c = sig == releaseSignal() ? 'r' : 'a';
						if (write(out, &c, 1) < 0) { }
					}
					errno = saved;
				}

				void run()
				{
					// the signals let through here (open blocked them for the others)
					sigset_t set;
					sigemptyset(&set);
					sigaddset(&set, releaseSignal());
					sigaddset(&set, acquireSignal());
					pthread_sigmask(SIG_UNBLOCK, &set, nullptr);

					while (!mStop.load())
					{
						pollfd p = { mPipeIn, POLLIN, 0 };
						const int ready = poll(&p, 1, mMisterMode ? 250 : -1);
						char c = 0;
						while (ready > 0 && read(mPipeIn, &c, 1) == 1)
						{
							if (c == 'q')
								return;
							if (c == 'r')
								release();
							else if (c == 'a')
								acquire();
						}
						if (mMisterMode && mPresenting.load())
							keepMisterMode();
					}
				}

				// Another console asked for: no more frames to the screen, the one being copied finished (at
				// most a fraction of a second), then the switch let through
				void release()
				{
					mPresenting.store(false);
					for (int wait = 0; wait < 200 && mCopying.load() > 0; wait++)
						usleep(1000);
					ioctl(mFd, VT_RELDISP, 1);
				}

				// Ours again: the next frame whole
				void acquire()
				{
					ioctl(mFd, VT_RELDISP, VT_ACKACQ);
					mRedraw.store(true);
					mPresenting.store(true);
					if (mMisterMode)
						keepMisterMode();
				}

				// The MiSTer's framebuffer at ES's size while ES's console is the one shown (Main_MiSTer writes
				// the mode it sets to the driver's parameters: at F9 its console's)
				void keepMisterMode()
				{
					if (mVt > 0)
					{
						char active[32] = "";
						FILE* f = fopen("/sys/devices/virtual/tty/tty0/active", "r");
						if (f == nullptr)
							return;
						const bool read = fgets(active, sizeof(active), f) != nullptr;
						fclose(f);
						int n = 0;
						if (!read || sscanf(active, "tty%d", &n) != 1 || n != mVt)
							return;
					}
					FILE* f = fopen("/sys/module/MiSTer_fb/parameters/mode", "r");
					if (f == nullptr)
						return;
					int format = 0, rb = 0, width = 0, height = 0;
					const int got = fscanf(f, "%d %d %d %d", &format, &rb, &width, &height);
					fclose(f);
					if (got != 4 || (width == mWidth && height == mHeight))
						return;
					FILE* cmd = fopen("/dev/MiSTer_cmd", "w");
					if (cmd == nullptr)
						return;
					fprintf(cmd, "fb_cmd1 8888 1 %d %d\n", mWidth, mHeight);
					fclose(cmd);
					mRedraw.store(true);
				}

				int mFd = -1, mVt = 0, mPipeIn = -1;
				int mWidth = 0, mHeight = 0;
				bool mProcess = false, mMisterMode = false;
				std::atomic<bool> mPresenting{ true }, mStop{ false }, mRedraw{ false };
				std::atomic<int> mCopying{ 0 };
				std::thread mThread;
			};

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
					{
						// a framebuffer's memory smaller than its mode: only the rows it holds are written
						mSize = fix.smem_len;
						mHeight = (int)(mSize / mPitch);
					}
					if (mPitch <= 0 || mWidth <= 0 || mHeight <= 0 || (size_t)mWidth * (mBpp / 8) > (size_t)mPitch)
					{
						error = "unusable framebuffer mode " + std::to_string(mWidth) + "x" + std::to_string(mHeight) + ", " + std::to_string(mPitch) + " bytes a row";
						close();
						return false;
					}

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

					// the console stops drawing (its text and its cursor) over the frames, and lets another
					// console have the screen when asked (Console)
					mConsoleHeld = mConsole.open(mWidth, mHeight, env("ES_SOFT_MISTER_MODE") == "1");

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
					if (mConsoleHeld)
					{
						mConsole.close();
						mConsoleHeld = false;
					}
				}

				bool takeRedraw() override { return mConsoleHeld && mConsole.takeRedraw(); }

				// Straight to the framebuffer: the renderer gives only what changed, so no copy of the screen is
				// kept to compare rows with (that read and wrote each row twice more)
				int64_t presentRect(const uint32_t* pixels, int width, int height, int x0, int y0, int x1, int y1) override
				{
					if (mMap == nullptr)
						return 0;
					// another console has the screen: nothing goes to it
					if (mConsoleHeld && !mConsole.begin())
						return 0;
					struct Done
					{
						Console* console;
						~Done() { if (console) console->done(); }
					} done{ mConsoleHeld ? &mConsole : nullptr };
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
				int mFd = -1;
				Console mConsole;
				bool mConsoleHeld = false;
				uint8_t* mMap = nullptr;
				size_t mSize = 0;
				int mWidth = 0, mHeight = 0, mBpp = 0, mPitch = 0;
				fb_bitfield mRed = {}, mGreen = {}, mBlue = {}, mAlpha = {};
				bool mNative = false;
			};
#endif
		}

		std::unique_ptr<Output> createOutput(const char* requested)
		{
			std::string kind = requested != nullptr ? requested : env("ES_SOFT_OUTPUT");
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
