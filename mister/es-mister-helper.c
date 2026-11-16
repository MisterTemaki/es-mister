// es-mister-helper: what the MiSTer scripts (es-mister.sh, mister-launch.sh) need that a shell cannot
// do. Main_MiSTer shows the Linux framebuffer (its "terminal") when F9 is pressed in the menu core, and
// takes fb_cmd commands on /dev/MiSTer_cmd only while it shows it; a virtual keyboard (uinput) presses
// F9 for us. As mrext does it: switch to an unused console first, press F9, and know it worked when
// the active console became tty1 (Main_MiSTer switches to it); repeat, since a sleeping menu eats the
// first key.
//
//   es-mister-helper console WIDTHxHEIGHT   the Linux framebuffer on, at that size (fb_cmd1), 0 on success
//   es-mister-helper console auto          the same, at the size Main_MiSTer gives it for the menu (from
//                                          video_mode and fb_size in MiSTer.ini); the size is printed
//   es-mister-helper console auto/N        that size divided by N (auto/2: 960x540 on a 1080p screen)
//   es-mister-helper console fill:WxH      for ES drawing at WxH and enlarging its frames to the framebuffer
//                                          (ES_SOFT_DRAW_SIZE): at the screen's size (as auto), or at WxH
//                                          when the MiSTer's scaler fills the screen with it (a whole number
//                                          of times both ways); the size is printed
//   es-mister-helper console SIZE PRESSES  at most PRESSES presses of F9 (default 30, about 10 s)
//   es-mister-helper key NAME               a key pressed and let go (F9, F12, ESC, ENTER)
//   es-mister-helper core                   the core loaded now (/tmp/CORENAME)
//   es-mister-helper text                   the console back in text mode, after ES ended (see text_mode)
//   es-mister-helper guard PID              while PID lives: no console switch waits more than a second
//                                          (see guard)
//   es-mister-helper osd                    the MiSTer menu opened (F12) over ES, which asked first (see osd)
//   es-mister-helper ungrab PID             PID's hold on the controllers let go of (see ungrab)
//   es-mister-helper reboot                 the MiSTer restarted cold, as its menu's Reboot held (see power)
//   es-mister-helper halt                   the MiSTer shut down, its HDMI output off (see power)

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/i2c-dev.h>
#include <linux/uinput.h>
#include <linux/vt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/reboot.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static void sleep_ms(int ms)
{
	struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L };
	nanosleep(&t, NULL);
}

static int read_line(const char* path, char* out, size_t size)
{
	FILE* f = fopen(path, "r");
	if (!f)
		return 0;
	if (!fgets(out, (int)size, f))
		out[0] = 0;
	fclose(f);
	out[strcspn(out, "\r\n")] = 0;
	return 1;
}

static int keyboard_open(void)
{
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (fd < 0)
	{
		perror("es-mister-helper: /dev/uinput");
		return -1;
	}

	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_EVBIT, EV_SYN);
	// a whole keyboard's keys, so Main_MiSTer takes it for one
	for (int key = 1; key < 256; key++)
		ioctl(fd, UI_SET_KEYBIT, key);

	struct uinput_setup setup;
	memset(&setup, 0, sizeof(setup));
	setup.id.bustype = BUS_USB;
	setup.id.vendor = 0x1209;
	setup.id.product = 0xE5E5;
	strcpy(setup.name, "EmulationStation keyboard");
	if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0)
	{
		perror("es-mister-helper: uinput");
		close(fd);
		return -1;
	}

	// Main_MiSTer finds new input devices on its own
	sleep_ms(1000);
	return fd;
}

static void emit(int fd, int type, int code, int value)
{
	struct input_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = type;
	ev.code = code;
	ev.value = value;
	if (write(fd, &ev, sizeof(ev)) < 0)
		perror("es-mister-helper: write");
}

static void press(int fd, int key)
{
	emit(fd, EV_KEY, key, 1);
	emit(fd, EV_SYN, SYN_REPORT, 0);
	sleep_ms(40);
	emit(fd, EV_KEY, key, 0);
	emit(fd, EV_SYN, SYN_REPORT, 0);
}

static void keyboard_close(int fd)
{
	sleep_ms(100);
	ioctl(fd, UI_DEV_DESTROY);
	close(fd);
}

static int key_code(const char* name)
{
	if (!strcmp(name, "F9")) return KEY_F9;
	if (!strcmp(name, "F12")) return KEY_F12;
	if (!strcmp(name, "ESC")) return KEY_ESC;
	if (!strcmp(name, "ENTER")) return KEY_ENTER;
	if (!strcmp(name, "UP")) return KEY_UP;
	if (!strcmp(name, "DOWN")) return KEY_DOWN;
	return 0;
}

// The active console, as /sys has it (tty1...): its number, 0 when unknown
static int active_console(void)
{
	char tty[32] = "";
	int n = 0;
	read_line("/sys/devices/virtual/tty/tty0/active", tty, sizeof(tty));
	return sscanf(tty, "tty%d", &n) == 1 ? n : 0;
}

// Switches to console n, waiting at most a second for it. Not VT_WAITACTIVE: it waits for ever when
// the switch does not happen, as when the console is in graphics mode (the kernel ignores switches
// then), and the helper hung there after a game while ES held the console.
static int chvt(int n)
{
	int fd = open("/dev/tty0", O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return 0;
	// in graphics mode the kernel ignores the switch, unless the program drawing there is asked first
	// (VT_PROCESS: ES holds its console that way and lets it go)
	int mode = KD_TEXT;
	struct vt_mode held = { 0 };
	if (ioctl(fd, KDGETMODE, &mode) == 0 && mode == KD_GRAPHICS &&
		!(ioctl(fd, VT_GETMODE, &held) == 0 && held.mode == VT_PROCESS))
		fprintf(stderr, "es-mister-helper: the console is in graphics mode (a program drawing on it holds it): it cannot switch\n");
	int ok = ioctl(fd, VT_ACTIVATE, n) == 0;
	close(fd);
	for (int wait = 0; ok && wait < 20 && active_console() != n; wait++)
		sleep_ms(50);
	return ok && active_console() == n;
}

// The console in text mode again. ES puts it in graphics mode while it draws and puts it back when it
// closes; ended by a signal (killall, a crash, the kernel out of memory) it leaves it in graphics mode,
// where the kernel ignores console switches: F9 no longer showed the Linux framebuffer, and the MiSTer
// menu could not show a script, until the next reboot. es-mister.sh runs this once ES has ended.
//
// ES holds its console with VT_PROCESS (the kernel asks it before switching away) and turns its keys off
// (K_OFF); ended by a signal it leaves those too: every console (1 to 8) left in graphics mode, held or
// with its keys off is let go here, not only the active one (ES's may not be).
static int text_mode(void)
{
	int fd = open("/dev/tty0", O_RDWR | O_CLOEXEC);
	if (fd < 0)
	{
		perror("es-mister-helper: /dev/tty0");
		return 1;
	}
	int mode = KD_TEXT;
	if (ioctl(fd, KDGETMODE, &mode) == 0 && mode == KD_GRAPHICS)
		fprintf(stderr, "es-mister-helper: the console was left in graphics mode: back to text mode\n");
	const int ok = ioctl(fd, KDSETMODE, KD_TEXT) == 0;
	if (!ok)
		perror("es-mister-helper: KDSETMODE");
	close(fd);

	for (int n = 1; n <= 8; n++)
	{
		char path[32];
		snprintf(path, sizeof(path), "/dev/tty%d", n);
		int tty = open(path, O_RDWR | O_CLOEXEC | O_NOCTTY);
		if (tty < 0)
			continue;
		if (ioctl(tty, KDGETMODE, &mode) == 0 && mode == KD_GRAPHICS)
		{
			fprintf(stderr, "es-mister-helper: tty%d was left in graphics mode: back to text mode\n", n);
			ioctl(tty, KDSETMODE, KD_TEXT);
		}
		int keyboard = K_UNICODE;
		if (ioctl(tty, KDGKBMODE, &keyboard) == 0 && keyboard == K_OFF)
		{
			fprintf(stderr, "es-mister-helper: tty%d was left with its keys off: on again\n", n);
			ioctl(tty, KDSKBMODE, K_UNICODE);
		}
		struct vt_mode vt;
		if (ioctl(tty, VT_GETMODE, &vt) == 0 && vt.mode == VT_PROCESS)
		{
			fprintf(stderr, "es-mister-helper: tty%d was left held: let go\n", n);
			memset(&vt, 0, sizeof(vt));
			vt.mode = VT_AUTO;
			ioctl(tty, VT_SETMODE, &vt);
		}
		close(tty);
	}
	return ok ? 0 : 1;
}

// A process waiting in VT_WAITACTIVE (ioctl, 0x5607) for console n, as /proc/PID/syscall shows it (ARM's
// ioctl is 54, x86-64's 16): n, 0 when it is not
static int waiting_for_console(const char* pid)
{
	char path[300], line[256];
	snprintf(path, sizeof(path), "/proc/%s/syscall", pid);
	if (!read_line(path, line, sizeof(line)))
		return 0;
	long nr = -1;
	unsigned long fd = 0, request = 0, arg = 0;
	if (sscanf(line, "%ld 0x%lx 0x%lx 0x%lx", &nr, &fd, &request, &arg) != 4)
		return 0;
	if ((nr != 54 && nr != 16) || request != VT_WAITACTIVE)
		return 0;
	return (int)arg;
}

// While ES (pid) runs: whenever a process (Main_MiSTer running a script: F12 and Scripts) waits more than
// a second for a switch away from a console held by ES (VT_PROCESS) that ES has not let go, the switch is
// let go here (VT_RELDISP on that console, as ES would). ES answers such a switch itself at once; this is
// the guard for when it does not, whatever the reason: the MiSTer's menu waited for ever then.
// the first process of that name (/proc/N/comm), 0 when none
static pid_t process_named(const char* wanted)
{
	DIR* dir = opendir("/proc");
	if (!dir)
		return 0;
	pid_t pid = 0;
	struct dirent* entry;
	while (!pid && (entry = readdir(dir)) != NULL)
	{
		const pid_t n = (pid_t)atoi(entry->d_name);
		if (n <= 1)
			continue;
		char path[64], name[64] = "";
		snprintf(path, sizeof(path), "/proc/%d/comm", (int)n);
		if (read_line(path, name, sizeof(name)) && !strcmp(name, wanted))
			pid = n;
	}
	closedir(dir);
	return pid;
}

// ES's hold on the controllers (EVIOCGRAB, MisterPads in ES: the MiSTer menu's button asks first while ES
// is shown) let go of from outside: each of PID's input descriptors taken (pidfd_getfd: the same open
// file, whose grab it is) and let go of. ES does it itself when its console is let go of and before a game;
// this is for when it could not (stuck), so that the controllers never stay away from the MiSTer.
#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif
#ifndef SYS_pidfd_getfd
#define SYS_pidfd_getfd 438
#endif
static int ungrab(int pid)
{
	const int pidfd = (int)syscall(SYS_pidfd_open, pid, 0);
	if (pidfd < 0)
		return 0;
	char dir_path[32];
	snprintf(dir_path, sizeof(dir_path), "/proc/%d/fd", pid);
	DIR* dir = opendir(dir_path);
	int released = 0;
	if (dir)
	{
		struct dirent* e;
		while ((e = readdir(dir)) != NULL)
		{
			if (e->d_name[0] < '0' || e->d_name[0] > '9')
				continue;
			char link[64], target[128];
			snprintf(link, sizeof(link), "%s/%s", dir_path, e->d_name);
			const ssize_t n = readlink(link, target, sizeof(target) - 1);
			if (n <= 0)
				continue;
			target[n] = 0;
			if (strncmp(target, "/dev/input/event", 16) != 0)
				continue;
			const int fd = (int)syscall(SYS_pidfd_getfd, pidfd, atoi(e->d_name), 0);
			if (fd < 0)
				continue;
			// 0 only where this open file held it
			if (ioctl(fd, EVIOCGRAB, 0) == 0)
				released++;
			close(fd);
		}
		closedir(dir);
	}
	close(pidfd);
	if (released)
		fprintf(stderr, "es-mister-helper: %d controller(s) let go of by %d\n", released, pid);
	return released;
}

// The MiSTer menu over ES (its controllers given back already): F12 until Main_MiSTer takes the screen
// from ES's console (a sleeping menu may miss the first press), at most 3 presses
static int osd(void)
{
	const int before = active_console();
	int kbd = keyboard_open();
	if (kbd < 0)
		return 1;
	int done = 0;
	for (int tries = 0; tries < 3 && !done; tries++)
	{
		press(kbd, KEY_F12);
		for (int wait = 0; wait < 30 && !done; wait++)
		{
			sleep_ms(50);
			done = active_console() != before;
		}
	}
	keyboard_close(kbd);
	if (!done)
		fprintf(stderr, "es-mister-helper: the MiSTer menu did not take the screen (F12)\n");
	return done ? 0 : 1;
}

static int guard(int es)
{
	signal(SIGPIPE, SIG_IGN);
	int waited = 0; // quarters of a second something has waited for a switch
	while (kill(es, 0) == 0 || errno == EPERM)
	{
		sleep_ms(250);
		const int active = active_console();
		int wanted = 0;
		DIR* dir = opendir("/proc");
		if (dir)
		{
			struct dirent* e;
			while ((e = readdir(dir)) != NULL && !wanted)
			{
				if (e->d_name[0] < '0' || e->d_name[0] > '9')
					continue;
				const int n = waiting_for_console(e->d_name);
				if (n > 0 && n != active)
					wanted = n;
			}
			closedir(dir);
		}
		waited = wanted ? waited + 1 : 0;
		if (waited < 4 || active <= 0)
			continue;

		char path[32];
		snprintf(path, sizeof(path), "/dev/tty%d", active);
		int tty = open(path, O_RDWR | O_CLOEXEC | O_NOCTTY);
		if (tty < 0)
			continue;
		struct vt_mode vt;
		if (ioctl(tty, VT_GETMODE, &vt) == 0 && vt.mode == VT_PROCESS)
		{
			const int ok = ioctl(tty, VT_RELDISP, 1) == 0;
			fprintf(stderr, "es-mister-helper: a switch from tty%d to tty%d waited %d ms: %s\n", active, wanted,
				waited * 250, ok ? "let go here" : strerror(errno));
			// ES did not answer: the controllers it may hold given back too
			const pid_t held = process_named("emulationstation");
			if (held > 0)
				ungrab(held);
		}
		close(tty);
		waited = 0;
	}
	return 0;
}

// the Linux framebuffer's size as it is now; 0 when it cannot be read
static int framebuffer_size(int* width, int* height)
{
	int fd = open("/dev/fb0", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return 0;
	struct fb_var_screeninfo var;
	int ok = ioctl(fd, FBIOGET_VSCREENINFO, &var) == 0 && var.xres > 0 && var.yres > 0;
	close(fd);
	if (ok)
	{
		*width = (int)var.xres;
		*height = (int)var.yres;
	}
	return ok;
}

static int console_on(const char* size, int presses)
{
	int width = 640, height = 360, divisor = 1;
	const int fill = size && !strncmp(size, "fill:", 5);
	const int automatic = !fill && (!size || !strncmp(size, "auto", 4));
	if (automatic && size && size[4] == '/' && (sscanf(size + 5, "%d", &divisor) != 1 || divisor < 1 || divisor > 8))
	{
		fprintf(stderr, "es-mister-helper: auto/N with N from 1 to 8, not %s\n", size);
		return 2;
	}
	if (!automatic && (sscanf(fill ? size + 5 : size, "%dx%d", &width, &height) != 2 || width < 16 || height < 16))
	{
		fprintf(stderr, "es-mister-helper: size as WIDTHxHEIGHT, auto, auto/N or fill:WIDTHxHEIGHT, not %s\n", size);
		return 2;
	}

	char core[64] = "";
	read_line("/tmp/CORENAME", core, sizeof(core));
	if (strcmp(core, "MENU") != 0)
	{
		fprintf(stderr, "es-mister-helper: the menu core is not loaded (%s)\n", core);
		return 1;
	}

	int kbd = keyboard_open();
	if (kbd < 0)
		return 1;

	// a console Main_MiSTer does not use, so its switch to tty1 can be seen
	char before[32] = "";
	read_line("/sys/devices/virtual/tty/tty0/active", before, sizeof(before));
	chvt(3);
	sleep_ms(100);

	char tty[32] = "";
	int done = 0;
	for (int tries = 0; tries < presses && !done; tries++)
	{
		press(kbd, KEY_F9);
		for (int wait = 0; wait < 6 && !done; wait++)
		{
			sleep_ms(50);
			read_line("/sys/devices/virtual/tty/tty0/active", tty, sizeof(tty));
			done = strcmp(tty, "tty1") == 0;
		}
	}
	keyboard_close(kbd);

	if (!done)
	{
		fprintf(stderr, "es-mister-helper: Main_MiSTer did not switch to the Linux framebuffer "
			"(console %s before, %s after %d F9 presses)\n", before, tty, presses);
		return 1;
	}

	// auto: the size Main_MiSTer gave the framebuffer for the menu, which follows the screen's
	sleep_ms(200);
	if (automatic)
	{
		if (framebuffer_size(&width, &height))
		{
			width /= divisor;
			height /= divisor;
		}
		else
		{
			fprintf(stderr, "es-mister-helper: cannot read the framebuffer's size, 640x360\n");
			width = 640;
			height = 360;
		}
	}
	else if (fill)
	{
		// the screen's size, which ES's frames are enlarged to; WxH itself when the MiSTer's scaler
		// enlarges it to the screen (960x540 on 1080p: twice), which costs ES nothing
		int screen_width = 0, screen_height = 0;
		if (!framebuffer_size(&screen_width, &screen_height) || screen_width <= 0 || screen_height <= 0)
			fprintf(stderr, "es-mister-helper: cannot read the framebuffer's size, %dx%d\n", width, height);
		else if (!(screen_width % width == 0 && screen_height % height == 0 && screen_width / width == screen_height / height))
		{
			width = screen_width;
			height = screen_height;
		}
	}

	// the framebuffer at that size, 32 bits a pixel
	FILE* cmd = fopen("/dev/MiSTer_cmd", "w");
	if (!cmd)
	{
		perror("es-mister-helper: /dev/MiSTer_cmd");
		return 1;
	}
	fprintf(cmd, "fb_cmd1 8888 1 %d %d\n", width, height);
	fclose(cmd);
	sleep_ms(300); // Main_MiSTer sets the framebuffer driver's mode
	printf("%dx%d\n", width, height);
	return 0;
}

// U-Boot's boot flag, which Main_MiSTer sets before it restarts the board (fpga_io.cpp, reboot): 0 a
// cold boot, which loads the MiSTer as at power on; its magic value a warm one
static int boot_flag_cold(void)
{
	int fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
	if (fd < 0)
	{
		perror("es-mister-helper: /dev/mem");
		return 0;
	}
	void* map = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x1FFFF000);
	close(fd);
	if (map == MAP_FAILED)
	{
		perror("es-mister-helper: the boot flag");
		return 0;
	}
	*(volatile uint32_t*)((uint8_t*)map + 0xF08) = 0;
	munmap(map, 0x1000);
	return 1;
}

// The HDMI transmitter (the ADV7513, 0x39 on an I2C bus) powered down, as Main_MiSTer does when the screen
// goes away (tmds_power: register 0x41 from 0x10 to 0x50): the screen sees no signal
static int hdmi_off(void)
{
	static const int buses[] = { 1, 0, 2, 3 };
	for (size_t i = 0; i < sizeof(buses) / sizeof(buses[0]); i++)
	{
		char path[32];
		snprintf(path, sizeof(path), "/dev/i2c-%d", buses[i]);
		int fd = open(path, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			continue;
		unsigned char reg = 0x41, value = 0;
		// only where the transmitter answers
		if (ioctl(fd, I2C_SLAVE_FORCE, 0x39) == 0 && write(fd, &reg, 1) == 1 && read(fd, &value, 1) == 1)
		{
			const unsigned char out[2] = { 0x41, (unsigned char)(value | 0x40) };
			const int ok = write(fd, out, 2) == 2;
			close(fd);
			return ok;
		}
		close(fd);
	}
	fprintf(stderr, "es-mister-helper: no HDMI transmitter found\n");
	return 0;
}

// Main_MiSTer stopped (so that it does not turn the HDMI output on again), up to 2 s
static void stop_main(void)
{
	const pid_t pid = process_named("MiSTer");
	if (!pid || kill(pid, SIGTERM) != 0)
		return;
	for (int i = 0; i < 20 && kill(pid, 0) == 0; i++)
		sleep_ms(100);
	kill(pid, SIGKILL);
}

// ES's RESTART SYSTEM and SHUTDOWN OPERATING SYSTEM (es-mister.sh runs this once ES has closed).
//   reboot: as the MiSTer menu's "Reboot (hold for cold reboot)" held: the boot flag set to cold, then the
//           system's reboot, which writes and unmounts the SD card before the board restarts.
//   halt:   the DE10-Nano cannot cut its own power: Main_MiSTer stopped, the HDMI output off, and the
//           system shut down (the SD card unmounted: then it is safe to unplug; the board's LEDs stay on).
// Either way, if init's shutdown has not ended within 30 s, the kernel's own at once: never stuck.
static int power(int off)
{
	sync();
	if (off)
	{
		stop_main();
		hdmi_off();
	}
	else
		boot_flag_cold();
	sync();

	// the watch: killed with every process by init's shutdown, when it goes as it should
	const pid_t watch = fork();
	if (watch == 0)
	{
		setsid();
		sleep(30);
		sync();
		reboot(off ? RB_HALT_SYSTEM : RB_AUTOBOOT);
		_exit(1);
	}

	execl(off ? "/sbin/halt" : "/sbin/reboot", off ? "halt" : "reboot", (char*)NULL);
	perror("es-mister-helper: init's shutdown");
	sync();
	reboot(off ? RB_HALT_SYSTEM : RB_AUTOBOOT);
	return 1;
}

int main(int argc, char** argv)
{
	if (argc >= 2 && !strcmp(argv[1], "console"))
	{
		const int presses = argc >= 4 ? atoi(argv[3]) : 30;
		return console_on(argc >= 3 ? argv[2] : NULL, presses >= 1 && presses <= 100 ? presses : 30);
	}

	if (argc >= 3 && !strcmp(argv[1], "key"))
	{
		int code = key_code(argv[2]);
		if (!code)
		{
			fprintf(stderr, "es-mister-helper: unknown key %s\n", argv[2]);
			return 2;
		}
		int kbd = keyboard_open();
		if (kbd < 0)
			return 1;
		press(kbd, code);
		keyboard_close(kbd);
		return 0;
	}

	if (argc >= 2 && !strcmp(argv[1], "core"))
	{
		char core[64] = "";
		read_line("/tmp/CORENAME", core, sizeof(core));
		puts(core);
		return 0;
	}

	if (argc >= 2 && !strcmp(argv[1], "text"))
		return text_mode();

	if (argc >= 3 && !strcmp(argv[1], "guard"))
		return guard(atoi(argv[2]));

	if (argc >= 2 && !strcmp(argv[1], "osd"))
		return osd();

	if (argc >= 3 && !strcmp(argv[1], "ungrab"))
		return ungrab(atoi(argv[2])) >= 0 ? 0 : 1;

	if (argc >= 2 && (!strcmp(argv[1], "reboot") || !strcmp(argv[1], "halt")))
		return power(!strcmp(argv[1], "halt"));

	fprintf(stderr, "usage: %s console WIDTHxHEIGHT|auto|auto/N|fill:WIDTHxHEIGHT | key NAME | core | text | guard PID | osd | ungrab PID | reboot | halt\n", argv[0]);
	return 2;
}
