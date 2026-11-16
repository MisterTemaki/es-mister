#include "MisterPads.h"

#if defined(MISTER) && defined(__linux__)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Renderer
{
	namespace Soft
	{
		namespace MisterPads
		{
			namespace
			{
				struct Pad
				{
					int fd;
					dev_t device;
					std::string path;
					int buttons[2]; // SDL's indexes of the two codes held together (the same one twice: one button)
					bool held[2];
				};

				std::mutex lock;
				std::vector<Pad> pads;
				bool hiddenNow = false;
				bool givenBack = false; // until ES's console is shown again

				bool enabled()
				{
					static const bool on = []
					{
						const char* value = getenv("ES_MISTER_MENU_ASK");
						return value != nullptr && atoi(value) == 1;
					}();
					return on;
				}

				const int LONG_BITS = 8 * sizeof(unsigned long);
				bool testBit(const unsigned long* bits, int n) { return (bits[n / LONG_BITS] >> (n % LONG_BITS)) & 1; }

				// SDL's index of a button: its Linux joystick numbers the keys a device has from BTN_JOYSTICK up to
				// KEY_MAX, then those below (SDL_sysjoystick.c, ConfigJoystick)
				int sdlIndex(const unsigned long* keys, int code)
				{
					int index = 0;
					for (int i = BTN_JOYSTICK; i < KEY_MAX; i++)
					{
						if (i == code)
							return index;
						if (testBit(keys, i))
							index++;
					}
					for (int i = 0; i < BTN_JOYSTICK; i++)
					{
						if (i == code)
							return index;
						if (testBit(keys, i))
							index++;
					}
					return -1;
				}

				// Main_MiSTer's map for the controller: the two codes held together that open its menu
				// (input.h: SYS_BTN_OSD_KTGL + 1 and + 2, 21 and 22; the same code twice for one button)
				bool menuCodes(int vendor, int product, int codes[2])
				{
					char path[96];
					snprintf(path, sizeof(path), "/media/fat/config/inputs/input_%04x_%04x_v3.map", vendor, product);
					FILE* f = fopen(path, "rb");
					if (f == nullptr)
						return false;
					uint32_t map[32] = {};
					const size_t got = fread(map, sizeof(map[0]), 32, f);
					fclose(f);
					if (got < 23)
						return false;
					for (int i = 0; i < 2; i++)
					{
						const uint32_t code = map[21 + i];
						if (code == 0 || code >= KEY_MAX)
							return false;
						codes[i] = (int)code;
					}
					return true;
				}

				// the menu core loaded (a core loaded over ES, by Zaparoo or a script: its controllers)
				bool menuCore()
				{
					char name[64] = "";
					FILE* f = fopen("/tmp/CORENAME", "r");
					if (f == nullptr)
						return false;
					const bool got = fgets(name, sizeof(name), f) != nullptr;
					fclose(f);
					return got && strncmp(name, "MENU", 4) == 0 && (name[4] == 0 || name[4] == '\n');
				}

				bool sameDevice(const Pad& pad)
				{
					struct stat st;
					return fstat(pad.fd, &st) == 0 && st.st_rdev == pad.device;
				}

				void releaseLocked()
				{
					for (const Pad& pad : pads)
						if (sameDevice(pad))
							ioctl(pad.fd, EVIOCGRAB, 0);
					pads.clear();
				}

				// Is this descriptor SDL's joystick on a controller with a MiSTer menu button? Its pad then.
				bool padOf(int fd, const std::string& path, Pad& pad, std::string& name)
				{
					// SDL opens its joysticks to read and write (rumble), its keyboards and mice only to read
					const int flags = fcntl(fd, F_GETFL);
					if (flags < 0 || (flags & O_ACCMODE) != O_RDWR)
						return false;
					input_id id = {};
					if (ioctl(fd, EVIOCGID, &id) < 0 || id.bustype == BUS_VIRTUAL)
						return false;
					unsigned long keys[(KEY_MAX + 1 + 8 * sizeof(unsigned long) - 1) / (8 * sizeof(unsigned long))] = {};
					if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) < 0)
						return false;
					// a controller, not a keyboard
					if (!(testBit(keys, BTN_GAMEPAD) || testBit(keys, BTN_JOYSTICK)) || testBit(keys, KEY_A))
						return false;
					int codes[2];
					if (!menuCodes(id.vendor, id.product, codes) || !testBit(keys, codes[0]) || !testBit(keys, codes[1]))
						return false;
					struct stat st;
					if (fstat(fd, &st) < 0)
						return false;
					pad.fd = fd;
					pad.device = st.st_rdev;
					pad.path = path;
					for (int i = 0; i < 2; i++)
					{
						pad.buttons[i] = sdlIndex(keys, codes[i]);
						pad.held[i] = false;
					}
					char text[128] = "";
					if (ioctl(fd, EVIOCGNAME(sizeof(text) - 1), text) >= 0)
						name = text;
					return pad.buttons[0] >= 0 && pad.buttons[1] >= 0;
				}
			}

			void grab()
			{
				if (!enabled())
					return;
				std::lock_guard<std::mutex> guard(lock);
				if (hiddenNow || givenBack)
					return;
				if (!menuCore())
				{
					if (!pads.empty())
						fprintf(stderr, "MisterPads: a core was loaded: the controllers given back\n");
					releaseLocked();
					return;
				}

				// the descriptors SDL closed (a controller gone); those let go of from outside (es-mister-helper
				// ungrab) held again: a grab already held answers EBUSY
				for (size_t i = 0; i < pads.size();)
				{
					if (!sameDevice(pads[i]))
					{
						pads.erase(pads.begin() + i);
						continue;
					}
					if (ioctl(pads[i].fd, EVIOCGRAB, 1) == 0)
						fprintf(stderr, "MisterPads: %s was let go of: held again\n", pads[i].path.c_str());
					i++;
				}

				DIR* dir = opendir("/proc/self/fd");
				if (dir == nullptr)
					return;
				struct dirent* entry;
				while ((entry = readdir(dir)) != nullptr)
				{
					const int fd = atoi(entry->d_name);
					if (entry->d_name[0] < '0' || entry->d_name[0] > '9' || fd == dirfd(dir))
						continue;
					char link[64], target[128];
					snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
					const ssize_t n = readlink(link, target, sizeof(target) - 1);
					if (n <= 0)
						continue;
					target[n] = 0;
					if (strncmp(target, "/dev/input/event", 16) != 0)
						continue;
					bool known = false;
					for (const Pad& pad : pads)
						known = known || pad.fd == fd;
					if (known)
						continue;
					Pad pad;
					std::string name;
					if (!padOf(fd, target, pad, name))
						continue;
					// Main_MiSTer holds it (its menu shown, just closing): again in a second
					if (ioctl(fd, EVIOCGRAB, 1) < 0)
						continue;
					pads.push_back(pad);
					fprintf(stderr, "MisterPads: %s (%s): the MiSTer menu's button asks first (ES's while it is shown)\n", target, name.c_str());
				}
				closedir(dir);
			}

			void release()
			{
				std::lock_guard<std::mutex> guard(lock);
				if (!pads.empty())
					fprintf(stderr, "MisterPads: the controllers given back to Main_MiSTer\n");
				releaseLocked();
			}

			void shown(bool shown)
			{
				std::lock_guard<std::mutex> guard(lock);
				hiddenNow = !shown;
				if (shown)
					givenBack = false;
			}

			bool hidden()
			{
				std::lock_guard<std::mutex> guard(lock);
				return hiddenNow;
			}

			bool menuButton(const std::string& devicePath, int button, bool pressed, bool& ask)
			{
				ask = false;
				std::lock_guard<std::mutex> guard(lock);
				for (Pad& pad : pads)
				{
					if (pad.path != devicePath)
						continue;
					bool mine = false;
					for (int i = 0; i < 2; i++)
						if (pad.buttons[i] == button)
						{
							pad.held[i] = pressed;
							mine = true;
						}
					if (mine && pressed && pad.held[0] && pad.held[1])
						ask = true;
					return mine;
				}
				return false;
			}

			void giveBack(const char* why)
			{
				std::lock_guard<std::mutex> guard(lock);
				if (!pads.empty())
					fprintf(stderr, "MisterPads: %s: the controllers given back to Main_MiSTer\n", why);
				releaseLocked();
				givenBack = true;
			}
		}
	}
}

#else

namespace Renderer
{
	namespace Soft
	{
		namespace MisterPads
		{
			void grab() { }
			void release() { }
			void shown(bool) { }
			bool hidden() { return false; }
			bool menuButton(const std::string&, int, bool, bool& ask) { ask = false; return false; }
			void giveBack(const char*) { }
		}
	}
}

#endif
