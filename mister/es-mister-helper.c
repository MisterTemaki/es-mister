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
//   es-mister-helper key NAME               a key pressed and let go (F9, F12, ESC, ENTER)
//   es-mister-helper core                   the core loaded now (/tmp/CORENAME)

#include <fcntl.h>
#include <linux/fb.h>
#include <linux/uinput.h>
#include <linux/vt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
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

static int chvt(int n)
{
	int fd = open("/dev/tty0", O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return 0;
	int ok = ioctl(fd, VT_ACTIVATE, n) == 0;
	ioctl(fd, VT_WAITACTIVE, n);
	close(fd);
	return ok;
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

static int console_on(const char* size)
{
	int width = 640, height = 360, divisor = 1;
	const int automatic = !size || !strncmp(size, "auto", 4);
	if (automatic && size && size[4] == '/' && (sscanf(size + 5, "%d", &divisor) != 1 || divisor < 1 || divisor > 8))
	{
		fprintf(stderr, "es-mister-helper: auto/N with N from 1 to 8, not %s\n", size);
		return 2;
	}
	if (!automatic && sscanf(size, "%dx%d", &width, &height) != 2)
	{
		fprintf(stderr, "es-mister-helper: size as WIDTHxHEIGHT, auto or auto/N, not %s\n", size);
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
	chvt(3);
	sleep_ms(100);

	char tty[32] = "";
	int done = 0;
	for (int tries = 0; tries < 30 && !done; tries++)
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
		fprintf(stderr, "es-mister-helper: Main_MiSTer did not switch to the Linux framebuffer\n");
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

int main(int argc, char** argv)
{
	if (argc >= 2 && !strcmp(argv[1], "console"))
		return console_on(argc >= 3 ? argv[2] : NULL);

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

	fprintf(stderr, "usage: %s console WIDTHxHEIGHT|auto | key NAME | core\n", argv[0]);
	return 2;
}
