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
//   es-mister-helper cmd TEXT               TEXT (a line) to Main_MiSTer's command FIFO, 0 when taken (see send_cmd)
//   es-mister-helper key NAME               a key pressed and let go (F9, F12, ESC, ENTER)
//   es-mister-helper core                   the core loaded now (/tmp/CORENAME)
//   es-mister-helper text                   the console back in text mode, after ES ended (see text_mode)
//   es-mister-helper guard PID              while PID lives: no console switch waits more than a second
//                                          (see guard)
//   es-mister-helper ungrab PID             PID's hold on the controllers let go of (see ungrab)
//   es-mister-helper exitkeys PID           while PID (mister-launch.sh) waits for a game to end: Create and
//                                          Options (Share and Options) of a DualSense or DualShock 4 held
//                                          together for a second load the menu core (see exit_keys)
//   es-mister-helper bgm COMMAND            one command to the MiSTer BGM service (see bgm_command); its reply
//                                          printed; 1 when no service runs
//   es-mister-helper back PID SIZE          exitkeys, then console SIZE once the menu core is back, with the
//                                          keyboard made while the menu core loads: F9 before the MiSTer menu
//                                          is drawn (see back); the size is printed
//   es-mister-helper reboot                 the MiSTer restarted cold, as its menu's Reboot held (see power)
//   es-mister-helper halt                   the MiSTer shut down, its HDMI output off (see power)

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <strings.h>
#include <sys/inotify.h>
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
#include <sys/stat.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
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

static pid_t process_named(const char* wanted);

static long long now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

// A line on stderr (the log) after the time since the MiSTer started, as /proc/uptime and the scripts' lines
// have it
static void stamp(const char* format, ...)
{
	struct timespec t;
	clock_gettime(CLOCK_BOOTTIME, &t);
	fprintf(stderr, "[%lld.%02ld] es-mister-helper: ", (long long)t.tv_sec, t.tv_nsec / 10000000);
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputc('\n', stderr);
}

// the keyboard's /dev/input/eventN in node, "" when it cannot be told
static void keyboard_node(int fd, char* node, size_t size)
{
	char sysname[64] = "";
	node[0] = 0;
	if (ioctl(fd, UI_GET_SYSNAME(sizeof(sysname)), sysname) < 0)
		return;
	char path[160];
	snprintf(path, sizeof(path), "/sys/devices/virtual/input/%s", sysname);
	DIR* dir = opendir(path);
	if (!dir)
		return;
	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL)
		if (!strncmp(entry->d_name, "event", 5))
		{
			snprintf(node, size, "/dev/input/%s", entry->d_name);
			break;
		}
	closedir(dir);
}

// whether process pid has the file path open (one of /proc/pid/fd leads to it); with suffix set, any file whose
// name ends so (case ignored)
static int pid_has_open(pid_t pid, const char* path, int suffix)
{
	char fds[64];
	snprintf(fds, sizeof(fds), "/proc/%d/fd", (int)pid);
	DIR* dir = opendir(fds);
	if (!dir)
		return 0;
	const size_t length = strlen(path);
	int found = 0;
	struct dirent* entry;
	while (!found && (entry = readdir(dir)) != NULL)
	{
		if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
			continue;
		char link[96], target[512];
		snprintf(link, sizeof(link), "%s/%s", fds, entry->d_name);
		const ssize_t n = readlink(link, target, sizeof(target) - 1);
		if (n <= 0)
			continue;
		target[n] = 0;
		if (suffix)
			found = (size_t)n >= length && !strcasecmp(target + n - length, path);
		else
			found = !strcmp(target, path);
	}
	closedir(dir);
	return found;
}

// up to max processes of that name (/proc/N/comm), their number
static int processes_named(const char* wanted, pid_t* out, int max)
{
	DIR* dir = opendir("/proc");
	if (!dir)
		return 0;
	int count = 0;
	struct dirent* entry;
	while (count < max && (entry = readdir(dir)) != NULL)
	{
		const pid_t n = (pid_t)atoi(entry->d_name);
		if (n <= 1)
			continue;
		char path[64], name[64] = "";
		snprintf(path, sizeof(path), "/proc/%d/comm", (int)n);
		if (read_line(path, name, sizeof(name)) && !strcmp(name, wanted))
			out[count++] = n;
	}
	closedir(dir);
	return count;
}

// Waits until a Main_MiSTer has the keyboard node open, at most ms: the milliseconds it took, -1 when it did not.
// The node's opens are watched (inotify IN_OPEN), so an open is seen at once; besides, a look every poll_ms (in
// case no open is reported). Main_MiSTer starting again (a core loaded) is a new process: the processes named
// MiSTer are looked for again at each open and every 50 ms.
static int wait_main_opens(const char* node, int ms, int poll_ms, int* by_watch)
{
	int watch = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
	if (watch >= 0 && inotify_add_watch(watch, node, IN_OPEN) < 0)
	{
		close(watch);
		watch = -1;
	}
	const long long start = now_ms();
	long long scanned = -1000;
	pid_t mains[8];
	int count = 0, waited = -1, opened = 0;
	*by_watch = 0;
	for (;;)
	{
		const long long now = now_ms();
		if (opened || now - scanned >= 50)
		{
			count = processes_named("MiSTer", mains, 8);
			scanned = now;
		}
		for (int i = 0; i < count && waited < 0; i++)
			if (pid_has_open(mains[i], node, 0))
				waited = (int)(now - start);
		if (waited >= 0)
		{
			*by_watch = opened;
			break;
		}
		const int left = ms - (int)(now - start);
		if (left <= 0)
			break;
		opened = 0;
		if (watch >= 0)
		{
			struct pollfd p = { watch, POLLIN, 0 };
			if (poll(&p, 1, left < poll_ms ? left : poll_ms) > 0)
			{
				char events[4096];
				while (read(watch, events, sizeof(events)) > 0)
					opened = 1;
			}
		}
		else
			sleep_ms(left < poll_ms ? left : poll_ms);
	}
	if (watch >= 0)
		close(watch);
	return waited;
}

// Main_MiSTer finds new input devices on its own (it watches /dev/input): wait until it has opened this
// keyboard, its /dev/input/eventN among Main_MiSTer's open files, so the first F9 is not lost. At most
// MS milliseconds; 1 when it was opened, 0 when not, -1 when it cannot tell (then it waited MS).
static int keyboard_wait_opened(int fd, int ms)
{
	char node[64];
	keyboard_node(fd, node, sizeof(node));
	const pid_t mister = node[0] ? process_named("MiSTer") : 0;
	if (!mister)
	{
		sleep_ms(ms);
		return -1;
	}

	for (int waited = 0; waited < ms; waited += 10)
	{
		if (pid_has_open(mister, node, 0))
		{
			// a moment for Main_MiSTer to take it into its loop
			sleep_ms(50);
			fprintf(stderr, "es-mister-helper: Main_MiSTer opened the keyboard (%s) after %d ms\n", node, waited);
			return 1;
		}
		sleep_ms(10);
	}
	fprintf(stderr, "es-mister-helper: Main_MiSTer did not open the keyboard (%s) in %d ms\n", node, ms);
	return 0;
}

static int keyboard_create(void)
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
	return fd;
}

// The keyboard, once Main_MiSTer has it. Right after a core is loaded, Main_MiSTer starting again could miss
// a keyboard made meanwhile (between its look at /dev/input and its watch on it): it then took it only some
// 1.3 s later, 1 back from a game in 4. So a keyboard not taken within 0.25 s is made again (a new device
// Main_MiSTer sees), 4 times at most; then the last one is kept, as before.
static int keyboard_open(void)
{
	for (int attempt = 1;; attempt++)
	{
		const int fd = keyboard_create();
		if (fd < 0)
			return -1;
		const int last = attempt == 4;
		const int opened = keyboard_wait_opened(fd, last ? 1000 : 250);
		if (opened != 0 || last)
			return fd;
		ioctl(fd, UI_DEV_DESTROY);
		close(fd);
	}
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
	for (int wait = 0; ok && wait < 200 && active_console() != n; wait++)
		sleep_ms(5);
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
// A line to Main_MiSTer's command FIFO. Opened without waiting (O_NONBLOCK: no reader, ENXIO at once) and tried
// again for up to 10 s: a Main_MiSTer starting again (the menu core loaded, after a game or when ES ends) makes the
// FIFO anew, and a plain open made meanwhile waits on the old one for ever, with whatever waits for it (ES, while
// it starts a game). 0 when written whole.
// One command to the MiSTer BGM service (mrext's bgm.sh, or the older Python one: the same protocol), which plays
// music while the menu core is loaded: on its socket /tmp/bgm.sock, one command per connection ("stop", "play",
// "get playback", "set playback disabled"...), then its reply, if any, until it closes the connection. The reply
// is printed. 1 when no service listens (no socket, or a dead one's), or it did not answer within 5 s.
static int bgm_command(const char* command)
{
	const char* path = getenv("ES_BGM_SOCKET");
	if (!path || !*path)
		path = "/tmp/bgm.sock";
	const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return 1;
	struct sockaddr_un address;
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);
	// "stop" waits for the track playing to end (its player killed): a moment
	const struct timeval wait = { 5, 0 };
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof(wait));
	if (connect(fd, (struct sockaddr*)&address, sizeof(address)) < 0 ||
		write(fd, command, strlen(command)) != (ssize_t)strlen(command))
	{
		close(fd);
		return 1;
	}
	char reply[4096];
	size_t got = 0;
	while (got < sizeof(reply) - 1)
	{
		const ssize_t n = read(fd, reply + got, sizeof(reply) - 1 - got);
		if (n == 0)
			break;
		if (n < 0)
		{
			close(fd);
			return 1;
		}
		got += (size_t)n;
	}
	close(fd);
	reply[got] = 0;
	if (got)
		printf("%s\n", reply);
	return 0;
}

static int send_cmd(const char* text)
{
	char line[1024];
	const int length = snprintf(line, sizeof(line), "%s\n", text);
	if (length <= 0 || length >= (int)sizeof(line))
	{
		fprintf(stderr, "es-mister-helper: command too long\n");
		return 1;
	}
	for (int tries = 0; tries < 100; tries++)
	{
		const int fd = open("/dev/MiSTer_cmd", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd >= 0)
		{
			// a line under PIPE_BUF goes into the FIFO whole or not at all
			const ssize_t written = write(fd, line, (size_t)length);
			close(fd);
			if (written == length)
				return 0;
		}
		else if (errno != ENXIO && errno != ENOENT && errno != EINTR)
		{
			perror("es-mister-helper: /dev/MiSTer_cmd");
			return 1;
		}
		if (tries == 0)
			fprintf(stderr, "es-mister-helper: Main_MiSTer is not reading its commands yet, \"%s\" tried again\n", text);
		sleep_ms(100);
	}
	fprintf(stderr, "es-mister-helper: Main_MiSTer did not take \"%s\"\n", text);
	return 1;
}

static int framebuffer_mode(int* width, int* height, int* bits)
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
		if (bits)
			*bits = (int)var.bits_per_pixel;
	}
	return ok;
}

static int framebuffer_size(int* width, int* height)
{
	return framebuffer_mode(width, height, NULL);
}

// F9 pressed (held 40 ms) until the active console is tty1 (Main_MiSTer's switch to the Linux framebuffer), at
// most presses times, 0.3 s apart, the console looked at every 5 ms: the presses it took, 0 when it did not
static int f9_until_tty1(int kbd, int presses, char* tty, size_t size, int* taken_ms)
{
	const long long start = now_ms();
	for (int tries = 1; tries <= presses; tries++)
	{
		emit(kbd, EV_KEY, KEY_F9, 1);
		emit(kbd, EV_SYN, SYN_REPORT, 0);
		const long long down = now_ms();
		int done = 0;
		for (int waited = 0; waited < 300 && !done; waited += 5)
		{
			sleep_ms(5);
			read_line("/sys/devices/virtual/tty/tty0/active", tty, size);
			done = strcmp(tty, "tty1") == 0;
			if (done)
				*taken_ms = (int)(now_ms() - start);
			if (done || now_ms() - down >= 40)
				break;
		}
		const long long held = now_ms() - down;
		if (held < 40)
			sleep_ms((int)(40 - held));
		emit(kbd, EV_KEY, KEY_F9, 0);
		emit(kbd, EV_SYN, SYN_REPORT, 0);
		for (long long waited = now_ms() - down; !done && waited < 300; waited = now_ms() - down)
		{
			sleep_ms(5);
			read_line("/sys/devices/virtual/tty/tty0/active", tty, size);
			done = strcmp(tty, "tty1") == 0;
			if (done)
				*taken_ms = (int)(now_ms() - start);
		}
		if (done)
			return tries;
	}
	return 0;
}

// kbd: a keyboard made while the menu core loaded (back), -1 for one made now
static int console_on(const char* size, int presses, int kbd)
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
		if (kbd >= 0)
			keyboard_close(kbd);
		return 1;
	}

	// Made while the menu core loaded: Main_MiSTer starting again finds it in its first look at /dev/input, which
	// comes after it has drawn and shown its menu's background (and written CORENAME); F9 the moment it has it, so
	// that one of its first turns reads F9 and shows the Linux framebuffer: the MiSTer menu on screen 0.18 s, not
	// 0.58 s as when the keyboard was made after CORENAME. Not opened within 1.5 s (it should be): a new keyboard,
	// as below.
	int early = kbd >= 0;
	if (early)
	{
		char node[64];
		keyboard_node(kbd, node, sizeof(node));
		// a console Main_MiSTer does not use (see below): already, unless something switched back meanwhile
		if (active_console() != 3)
			chvt(3);
		int by_watch = 0;
		const int waited = node[0] ? wait_main_opens(node, 1500, 10, &by_watch) : -1;
		if (waited >= 0)
			stamp("Main_MiSTer opened the keyboard (%s) %d ms after the menu core was seen (%s)", node, waited,
				by_watch ? "its open reported" : "seen looking");
		else
		{
			stamp("Main_MiSTer did not open the keyboard (%s) made while the menu core loaded: a new one", node);
			ioctl(kbd, UI_DEV_DESTROY);
			close(kbd);
			early = 0;
			kbd = -1;
		}
	}
	if (kbd < 0)
		kbd = keyboard_open();
	if (kbd < 0)
		return 1;

	// the framebuffer driver's count of mode sets: Main_MiSTer's F9 sets the mode (and the count goes up)
	// right after its switch to tty1
	char count_before[32] = "", count_now[32] = "";
	const int counted = read_line("/sys/module/MiSTer_fb/parameters/res_count", count_before, sizeof(count_before));

	// a console Main_MiSTer does not use, so its switch to tty1 can be seen
	char before[32] = "";
	read_line("/sys/devices/virtual/tty/tty0/active", before, sizeof(before));
	if (!early)
	{
		chvt(3);
		sleep_ms(100);
	}

	char tty[32] = "";
	int taken = 0;
	const int done = f9_until_tty1(kbd, presses, tty, sizeof(tty), &taken);
	if (early && done)
		stamp("F9 taken after %d ms (%d press%s)", taken, done, done > 1 ? "es" : "");
	keyboard_close(kbd);

	if (!done)
	{
		fprintf(stderr, "es-mister-helper: Main_MiSTer did not switch to the Linux framebuffer "
			"(console %s before, %s after %d F9 presses)\n", before, tty, presses);
		return 1;
	}

	// auto: the size Main_MiSTer gave the framebuffer for the menu, which follows the screen's, once it has
	// set it (the count moved; at most 0.4 s), 0.2 s where the driver has no count
	if (counted)
	{
		for (int waited = 0; waited < 400; waited += 10)
		{
			if (read_line("/sys/module/MiSTer_fb/parameters/res_count", count_now, sizeof(count_now)) && strcmp(count_now, count_before) != 0)
				break;
			sleep_ms(10);
		}
	}
	else
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

	// the framebuffer at that size, 32 bits a pixel. Main_MiSTer's F9 has set it so already when that size is
	// the whole screen's (fill, auto): fb_cmd1 would set the same mode again (the same scaler words), which
	// clears the framebuffer and costs 0.3 s, so it is asked only for another size
	int now_width = 0, now_height = 0, now_bits = 0;
	if (!framebuffer_mode(&now_width, &now_height, &now_bits) || now_width != width || now_height != height || now_bits != 32)
	{
		char command[64];
		snprintf(command, sizeof(command), "fb_cmd1 8888 1 %d %d", width, height);
		if (send_cmd(command) != 0)
			return 1;
		sleep_ms(300); // Main_MiSTer sets the framebuffer driver's mode
	}
	printf("%dx%d\n", width, height);
	return 0;
}

// Main_MiSTer's child running the Scripts menu's script on tty2 (the agetty, then the script it runs, named
// "script"): its pid, 0 when there is none
static pid_t scripts_child(void)
{
	pid_t mains[8];
	const int count = processes_named("MiSTer", mains, 8);
	DIR* dir = count > 0 ? opendir("/proc") : NULL;
	if (!dir)
		return 0;
	pid_t found = 0;
	struct dirent* entry;
	while (!found && (entry = readdir(dir)) != NULL)
	{
		const pid_t n = (pid_t)atoi(entry->d_name);
		if (n <= 1)
			continue;
		char path[64], stat[512];
		snprintf(path, sizeof(path), "/proc/%d/stat", (int)n);
		if (!read_line(path, stat, sizeof(stat)))
			continue;
		char* open_paren = strchr(stat, '(');
		char* close_paren = strrchr(stat, ')');
		char state = 0;
		int parent = 0;
		if (!open_paren || !close_paren || sscanf(close_paren + 1, " %c %d", &state, &parent) != 2 || state == 'Z')
			continue;
		*close_paren = 0;
		const char* name = open_paren + 1;
		for (int i = 0; i < count; i++)
			if (parent == mains[i] && (!strcmp(name, "script") || !strcmp(name, "agetty")))
				found = n;
	}
	closedir(dir);
	return found;
}

// A QOI picture (qoiformat.org) decoded to count pixels 0xAARRGGBB: the splash snapshot's (ES writes it with
// the reference encoder, external/qoi). 1 when it decoded whole.
static int qoi_decode_argb(const unsigned char* data, size_t length, uint32_t* out, size_t count)
{
	if (length < 14 + 8 || memcmp(data, "qoif", 4) != 0)
		return 0;
	const size_t chunks = length - 8;
	size_t p = 14;
	uint32_t index[64];
	memset(index, 0, sizeof(index));
	unsigned char r = 0, g = 0, b = 0, a = 255;
	int run = 0;
	for (size_t i = 0; i < count; i++)
	{
		if (run > 0)
			run--;
		else if (p < chunks)
		{
			const int b1 = data[p++];
			if (b1 == 0xfe)
			{
				if (p + 3 > chunks)
					return 0;
				r = data[p++]; g = data[p++]; b = data[p++];
			}
			else if (b1 == 0xff)
			{
				if (p + 4 > chunks)
					return 0;
				r = data[p++]; g = data[p++]; b = data[p++]; a = data[p++];
			}
			else if ((b1 & 0xc0) == 0x00)
			{
				const uint32_t px = index[b1];
				a = (unsigned char)(px >> 24); r = (unsigned char)(px >> 16); g = (unsigned char)(px >> 8); b = (unsigned char)px;
			}
			else if ((b1 & 0xc0) == 0x40)
			{
				r += ((b1 >> 4) & 0x03) - 2;
				g += ((b1 >> 2) & 0x03) - 2;
				b += (b1 & 0x03) - 2;
			}
			else if ((b1 & 0xc0) == 0x80)
			{
				if (p + 1 > chunks)
					return 0;
				const int b2 = data[p++];
				const int vg = (b1 & 0x3f) - 32;
				r += vg - 8 + ((b2 >> 4) & 0x0f);
				g += vg;
				b += vg - 8 + (b2 & 0x0f);
			}
			else
				run = b1 & 0x3f;
			index[(r * 3 + g * 5 + b * 7 + a * 11) % 64] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
		}
		else
			return 0;
		out[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
	}
	return 1;
}

// the mean of two pixels, each byte rounded up: as ES's SoftScale (NEON's vrhadd)
static inline uint32_t pixel_mean(uint32_t a, uint32_t b)
{
	return (a | b) - (((a ^ b) & 0xFEFEFEFEu) >> 1);
}

// ES's splash snapshot (ES_SPLASH_SNAPSHOT, written by ES at its first frame) on the framebuffer (map, of
// width x height pixels, rows of pitch bytes): as ES copies its frames to it, the same size or 1.5 times
// bigger (each 2x2 pixels to 3x3, the middle ones means, as SoftScale's three halves), so that ES's own
// first frame, the same picture, changes nothing. 0 when there is none or it does not fit (black then).
static int paint_snapshot(const char* path, uint8_t* map, int width, int height, size_t pitch)
{
	if (!path || !*path)
		return 0;
	const int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return 0;
	struct stat info;
	unsigned char* file = NULL;
	ssize_t got = -1;
	if (fstat(fd, &info) == 0 && info.st_size > 32 && info.st_size < 64 * 1024 * 1024 && (file = malloc((size_t)info.st_size)) != NULL)
		got = read(fd, file, (size_t)info.st_size);
	close(fd);
	if (!file || got != info.st_size)
	{
		free(file);
		return 0;
	}
	uint32_t head[3], qoi_length = 0;
	int ok = 0;
	if (memcmp(file, "ESSPLSH1", 8) == 0)
	{
		memcpy(head, file + 8, sizeof(head));
		const size_t at = 8 + sizeof(head) + head[2];
		if (head[2] < 4096 && at + 4 <= (size_t)got)
		{
			memcpy(&qoi_length, file + at, 4);
			const int w = (int)head[0], h = (int)head[1];
			const int same = w == width && h == height;
			const int half = (int64_t)w * 3 == (int64_t)width * 2 && (int64_t)h * 3 == (int64_t)height * 2 && w % 2 == 0 && h % 2 == 0;
			uint32_t* frame = (same || half) && at + 4 + qoi_length == (size_t)got ? malloc((size_t)w * h * 4) : NULL;
			if (frame && qoi_decode_argb(file + at + 4, qoi_length, frame, (size_t)w * h))
			{
				if (same)
					for (int y = 0; y < h; y++)
						memcpy(map + (size_t)y * pitch, frame + (size_t)y * w, (size_t)w * 4);
				else
					for (int y = 0; y < h; y += 2)
					{
						const uint32_t* top = frame + (size_t)y * w;
						const uint32_t* bottom = top + w;
						uint32_t* row0 = (uint32_t*)(map + (size_t)(y / 2 * 3) * pitch);
						uint32_t* row1 = (uint32_t*)((uint8_t*)row0 + pitch);
						uint32_t* row2 = (uint32_t*)((uint8_t*)row1 + pitch);
						for (int x = 0; x < w; x += 2)
						{
							const int o = x / 2 * 3;
							row0[o] = top[x];
							row0[o + 1] = pixel_mean(top[x], top[x + 1]);
							row0[o + 2] = top[x + 1];
							const uint32_t even = pixel_mean(top[x], bottom[x]), odd = pixel_mean(top[x + 1], bottom[x + 1]);
							row1[o] = even;
							row1[o + 1] = pixel_mean(even, odd);
							row1[o + 2] = odd;
							row2[o] = bottom[x];
							row2[o + 1] = pixel_mean(bottom[x], bottom[x + 1]);
							row2[o + 2] = bottom[x + 1];
						}
					}
				ok = 1;
			}
			free(frame);
		}
	}
	free(file);
	return ok;
}

// Started from the Scripts menu (es-mister.sh --scripts): while the script runs, Main_MiSTer shows the Linux
// framebuffer with the script's console (tty2), and emulationstation.sh stays until ES closes. ES's console
// shown there at once, without F9: Main_MiSTer takes F9 only once the script has ended, a key has closed its
// screen and it has drawn its menu's background again (3-4 s with a wallpaper, its keys unread meanwhile).
// tty1 put in graphics mode first (no console text drawn on it), switched to, and the framebuffer cleared to
// black until ES draws; then the framebuffer at the size asked, as console_on sets it. 1 when Main_MiSTer is
// not showing a script's console (es-mister.sh then asks for the framebuffer with F9).
static int scripts_screen(const char* size, const char* snapshot)
{
	int width = 640, height = 360;
	const int fill = size && !strncmp(size, "fill:", 5);
	if (!fill || sscanf(size + 5, "%dx%d", &width, &height) != 2 || width < 16 || height < 16)
	{
		fprintf(stderr, "es-mister-helper: scripts needs fill:WIDTHxHEIGHT, not %s\n", size ? size : "");
		return 2;
	}
	char core[64] = "";
	read_line("/tmp/CORENAME", core, sizeof(core));
	if (strcmp(core, "MENU") != 0 || active_console() != 2 || scripts_child() == 0)
	{
		fprintf(stderr, "es-mister-helper: no script's console shown (core %s, tty%d)\n", core, active_console());
		return 1;
	}

	// ES holding tty1 already (shown again after F12: es-mister.sh --show): it draws all again once it has the
	// console back; otherwise graphics mode and black here until ES draws
	int held = 0;
	const int tty = open("/dev/tty1", O_RDWR | O_CLOEXEC | O_NOCTTY);
	if (tty >= 0)
	{
		struct vt_mode mode;
		held = ioctl(tty, VT_GETMODE, &mode) == 0 && mode.mode == VT_PROCESS;
		if (!held)
			ioctl(tty, KDSETMODE, KD_GRAPHICS);
		close(tty);
	}
	if (!chvt(1))
	{
		fprintf(stderr, "es-mister-helper: the switch to tty1 did not happen (tty%d)\n", active_console());
		text_mode();
		return 1;
	}
	stamp("the script's console shown by Main_MiSTer: tty1 now, without F9");

	// the screen's size, which ES's frames are enlarged to (as console_on does for fill)
	int screen_width = 0, screen_height = 0;
	if (!framebuffer_size(&screen_width, &screen_height) || screen_width <= 0 || screen_height <= 0)
		fprintf(stderr, "es-mister-helper: cannot read the framebuffer's size, %dx%d\n", width, height);
	else if (!(screen_width % width == 0 && screen_height % height == 0 && screen_width / width == screen_height / height))
	{
		width = screen_width;
		height = screen_height;
	}
	int now_width = 0, now_height = 0, now_bits = 0;
	if (!framebuffer_mode(&now_width, &now_height, &now_bits) || now_width != width || now_height != height || now_bits != 32)
	{
		char command[64];
		snprintf(command, sizeof(command), "fb_cmd1 8888 1 %d %d", width, height);
		if (send_cmd(command) != 0)
			return 1;
		sleep_ms(300); // Main_MiSTer sets the framebuffer driver's mode
	}

	// ES's splash as it last drew it (snapshot), else black, until ES draws (the script's text stays in the
	// framebuffer otherwise)
	const int fb = held ? -1 : open("/dev/fb0", O_RDWR | O_CLOEXEC);
	if (fb >= 0)
	{
		struct fb_fix_screeninfo fix;
		struct fb_var_screeninfo var;
		if (ioctl(fb, FBIOGET_FSCREENINFO, &fix) == 0 && ioctl(fb, FBIOGET_VSCREENINFO, &var) == 0 && var.bits_per_pixel == 32)
		{
			const size_t length = (size_t)fix.line_length * var.yres;
			uint8_t* map = length > 0 && length <= fix.smem_len ? mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0) : MAP_FAILED;
			if (map != MAP_FAILED)
			{
				if (paint_snapshot(snapshot, map, (int)var.xres, (int)var.yres, fix.line_length))
					stamp("ES's splash painted (%s)", snapshot);
				else
					memset(map, 0, length);
				munmap(map, length);
			}
		}
		close(fb);
	}
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

// Back to ES from a game with the controller: Create and Options (a DualShock 4's Share and Options) held
// together for a second load the menu core, as the OSD's System > Reboot does, and ES comes back. During a
// game Main_MiSTer holds the controllers' input devices (EVIOCGRAB: their events go to it alone), so the
// controller is read from its raw HID reports (hidraw), which that hold leaves alone; Sony's pads only,
// whose reports the kernel's hid-playstation driver documents. Ends when PID ends or the menu core is
// loaded; nothing is held, nothing is changed in the reports.
enum { PAD_DUALSENSE = 1, PAD_DUALSHOCK4 = 2 };

// a hidraw node's pad kind from its HID_ID (bus:vendor:product), 0 when not a Sony pad
static int pad_kind(const char* node)
{
	char path[96], line[128];
	snprintf(path, sizeof(path), "/sys/class/hidraw/%s/device/uevent", node);
	FILE* f = fopen(path, "r");
	if (!f)
		return 0;
	int kind = 0;
	while (!kind && fgets(line, sizeof(line), f))
	{
		unsigned bus, vendor, product;
		if (sscanf(line, "HID_ID=%x:%x:%x", &bus, &vendor, &product) != 3 || vendor != 0x054c)
			continue;
		if (product == 0x0ce6 || product == 0x0df2) // DualSense, DualSense Edge
			kind = PAD_DUALSENSE;
		else if (product == 0x05c4 || product == 0x09cc || product == 0x0ba0) // DualShock 4 v1, v2, its USB dongle
			kind = PAD_DUALSHOCK4;
	}
	fclose(f);
	return kind;
}

// the report's second buttons byte (L1 R1 L2 R2 Create/Share Options L3 R3), -1 when the report has none:
// DualSense over Bluetooth 0x31 (78 bytes), over USB 0x01 (64); DualShock 4 over Bluetooth 0x11 (78), over
// USB 0x01 (64); either over Bluetooth before the driver asks for the full reports, 0x01 (10)
static int pad_buttons(int kind, const unsigned char* data, int size)
{
	if (size < 1)
		return -1;
	if (kind == PAD_DUALSENSE && data[0] == 0x31 && size >= 78)
		return data[10];
	if (kind == PAD_DUALSHOCK4 && data[0] == 0x11 && size >= 78)
		return data[8];
	if (data[0] == 0x01 && kind == PAD_DUALSENSE && size >= 64)
		return data[9];
	if (data[0] == 0x01 && size >= 10)
		return data[6];
	return -1;
}

#define EXIT_KEYS 0x30     // Create (Share) and Options in that byte
#define EXIT_HOLD_MS 1000

// The menu core being loaded: Main_MiSTer (the game's, pid) has menu.rbf open, which it reads and writes into the
// FPGA (2 to 3.5 s) before it starts again (System > Reboot, or load_core). Its pid looked for again when gone.
static int menu_core_loading(pid_t* mister)
{
	if (*mister <= 0 || kill(*mister, 0) != 0)
		*mister = process_named("MiSTer");
	return *mister > 0 && pid_has_open(*mister, "/menu.rbf", 1);
}

// keyboard: NULL, or where the keyboard made for back goes (-1 until then). With it, the keyboard is made as soon
// as the menu core is being loaded, and the console switched to tty3 (see console_on): the game is gone then, and
// the Main_MiSTer loading it reads no input until it starts again.
static int exit_keys(int launcher, int* keyboard)
{
	struct
	{
		int fd, kind;
		char node[32];
		long long since; // when both keys were down, 0 while not
	} pads[8];
	int count = 0;
	long long next_scan = 0;
	pid_t mister = 0;
	int asked = 0;

	for (;;)
	{
		if (kill(launcher, 0) != 0 && errno == ESRCH)
			break;
		char core[64] = "";
		if (read_line("/tmp/CORENAME", core, sizeof(core)) && !strncmp(core, "MENU", 4))
			break;
		if (keyboard && *keyboard < 0 && menu_core_loading(&mister))
		{
			*keyboard = keyboard_create();
			if (*keyboard >= 0)
			{
				char node[64];
				keyboard_node(*keyboard, node, sizeof(node));
				const int switched = chvt(3);
				stamp("the menu core is being loaded: keyboard %s made, console %s", node[0] ? node : "?",
					switched ? "3" : "not switched");
			}
		}

		// controllers connected meanwhile (a Bluetooth pad turned on during the game), every 2 s
		const long long now = now_ms();
		if (now >= next_scan)
		{
			next_scan = now + 2000;
			DIR* dir = opendir("/sys/class/hidraw");
			struct dirent* entry;
			while (dir && count < (int)(sizeof(pads) / sizeof(pads[0])) && (entry = readdir(dir)) != NULL)
			{
				if (strncmp(entry->d_name, "hidraw", 6) != 0 || strlen(entry->d_name) >= sizeof(pads[0].node))
					continue;
				int known = 0;
				for (int i = 0; i < count; i++)
					known |= !strcmp(pads[i].node, entry->d_name);
				const int kind = known ? 0 : pad_kind(entry->d_name);
				if (!kind)
					continue;
				char path[64];
				snprintf(path, sizeof(path), "/dev/%s", entry->d_name);
				const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
				if (fd < 0)
					continue;
				pads[count].fd = fd;
				pads[count].kind = kind;
				pads[count].since = 0;
				strcpy(pads[count].node, entry->d_name);
				fprintf(stderr, "es-mister-helper: %s, a %s: Create + Options held for a second go back to ES\n", entry->d_name,
					kind == PAD_DUALSENSE ? "DualSense" : "DualShock 4");
				count++;
			}
			if (dir)
				closedir(dir);
		}

		// a look every 50 ms at what came meanwhile (a DualSense sends some 250 reports a second: woken for each,
		// this costs 2.5% of a core, 0.5% so)
		sleep_ms(50);
		for (int i = 0; i < count; i++)
		{
			unsigned char report[128];
			ssize_t size;
			int buttons = -1;
			while ((size = read(pads[i].fd, report, sizeof(report))) > 0)
			{
				const int b = pad_buttons(pads[i].kind, report, (int)size);
				if (b >= 0)
					buttons = b;
			}
			if (size < 0 && errno != EAGAIN && errno != EINTR)
			{
				// gone (turned off): forgotten, found again by the next scan if it comes back
				close(pads[i].fd);
				pads[i] = pads[--count];
				i--;
				continue;
			}
			if (buttons < 0)
				continue;
			if ((buttons & EXIT_KEYS) != EXIT_KEYS)
				pads[i].since = 0;
			else if (!pads[i].since)
				pads[i].since = now_ms();
		}

		for (int i = 0; i < count; i++)
		{
			if (pads[i].since && now_ms() - pads[i].since >= EXIT_HOLD_MS)
			{
				fprintf(stderr, "es-mister-helper: Create + Options held on %s: the menu core, back to ES\n", pads[i].node);
				if (!keyboard)
				{
					for (int j = 0; j < count; j++)
						close(pads[j].fd);
					return send_cmd("load_core /media/fat/menu.rbf");
				}
				// back: the wait goes on until the menu core is there (its keyboard made meanwhile); asked once
				if (!asked && send_cmd("load_core /media/fat/menu.rbf") != 0)
					fprintf(stderr, "es-mister-helper: the menu core not asked for\n");
				asked = 1;
				pads[i].since = 0;
			}
		}
	}

	for (int i = 0; i < count; i++)
		close(pads[i].fd);
	return 0;
}

// Back from a game (mister-launch.sh, while it waits for the game's end): exitkeys, then console SIZE once the
// menu core is there, with the keyboard made while the menu core loads (exit_keys, console_on), so that F9 comes
// before the MiSTer menu is drawn. On CPU 0: Main_MiSTer runs on CPU 1 (it pins itself there), and its start is
// what this waits on. 1 when PID ended before the menu core came.
static int back(int launcher, const char* size)
{
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(0, &set);
	sched_setaffinity(0, sizeof(set), &set);

	int kbd = -1;
	exit_keys(launcher, &kbd);
	char core[64] = "";
	read_line("/tmp/CORENAME", core, sizeof(core));
	if (strcmp(core, "MENU") != 0)
	{
		if (kbd >= 0)
			keyboard_close(kbd);
		return 1;
	}
	stamp("the menu core (%s)", kbd >= 0 ? "its keyboard ready" : "no keyboard made yet");
	return console_on(size, 30, kbd);
}

int main(int argc, char** argv)
{
	if (argc >= 2 && !strcmp(argv[1], "console"))
	{
		const int presses = argc >= 4 ? atoi(argv[3]) : 30;
		return console_on(argc >= 3 ? argv[2] : NULL, presses >= 1 && presses <= 100 ? presses : 30, -1);
	}

	if (argc >= 3 && !strcmp(argv[1], "scripts"))
		return scripts_screen(argv[2], argc >= 4 ? argv[3] : NULL);
	// the Scripts menu's script ended and seen so by Main_MiSTer (no child of its running it), up to 3 s
	if (argc >= 2 && !strcmp(argv[1], "scripts-wait"))
	{
		for (int waited = 0; waited < 3000; waited += 10)
		{
			if (scripts_child() == 0)
				return 0;
			sleep_ms(10);
		}
		return 1;
	}

	if (argc >= 3 && !strcmp(argv[1], "bgm"))
		return bgm_command(argv[2]);

	if (argc >= 4 && !strcmp(argv[1], "back"))
		return back(atoi(argv[2]), argv[3]);

	if (argc >= 3 && !strcmp(argv[1], "cmd"))
		return send_cmd(argv[2]);
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

	if (argc >= 3 && !strcmp(argv[1], "ungrab"))
		return ungrab(atoi(argv[2])) >= 0 ? 0 : 1;

	if (argc >= 3 && !strcmp(argv[1], "exitkeys"))
		return exit_keys(atoi(argv[2]), NULL);

	if (argc >= 2 && (!strcmp(argv[1], "reboot") || !strcmp(argv[1], "halt")))
		return power(!strcmp(argv[1], "halt"));

	fprintf(stderr, "usage: %s console WIDTHxHEIGHT|auto|auto/N|fill:WIDTHxHEIGHT | cmd TEXT | key NAME | core | text | guard PID | ungrab PID | exitkeys PID | back PID SIZE | scripts fill:WIDTHxHEIGHT [SNAPSHOT] | scripts-wait | bgm COMMAND | reboot | halt\n", argv[0]);
	return 2;
}
