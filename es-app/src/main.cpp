//EmulationStation, a graphical front-end for ROM browsing. Created by Alec "Aloshi" Lofquist.
//http://www.aloshi.com

#include "services/HttpServerThread.h"
#include "guis/GuiDetectDevice.h"
#include "guis/GuiMsgBox.h"
#include "utils/FileSystemUtil.h"
#include "views/ViewController.h"
#include "CollectionSystemManager.h"
#include "EmulationStation.h"
#include "InputManager.h"
#include "Log.h"
#include "MameNames.h"
#include "Genres.h"
#include "utils/Platform.h"
#include "PowerSaver.h"
#include "Settings.h"
#include "SystemData.h"
#include "SystemScreenSaver.h"
#include <SDL_events.h>
#include <SDL_main.h>
#include <SDL_timer.h>
#include <iostream>
#include <time.h>
#include "LocaleES.h"
#include <SystemConf.h>
#include "ApiSystem.h"
#include "AudioManager.h"
#include "NetworkThread.h"
#include "scrapers/ThreadedScraper.h"
#include "ThreadedHasher.h"
#ifndef ES_USE_STB_IMAGE
#include <FreeImage.h>
#endif
#include "ImageIO.h"
#include "components/VideoVlcComponent.h"
#include <csignal>
#include "InputConfig.h"
#include "RetroAchievements.h"
#include "TextToSpeech.h"
#include "Paths.h"
#include "resources/TextureData.h"
#include "Scripting.h"
#include "watchers/WatchersManager.h"
#include "HttpReq.h"
#include <thread>
#include "ZaparooSupport.h"
#include "utils/ThreadPool.h"
#include "utils/StartupTrace.h"
#include "utils/Perf.h"
#include "utils/SampleProfiler.h"

#if defined(MISTER) && defined(__linux__)
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cstring>
#endif

#ifdef WIN32
#include <Windows.h>
#include <direct.h>
#define PATH_MAX MAX_PATH
#endif

static std::string gPlayVideo;
static int gPlayVideoDuration = 0;
static bool enable_startup_game = true;

bool parseArgs(int argc, char* argv[])
{
	Paths::setExePath(argv[0]);

	// We need to process --home before any call to Settings::getInstance(), because settings are loaded from homepath
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--home") == 0)
		{
			if (i == argc - 1)
				continue;

			std::string arg = argv[i + 1];
			if (arg.find("-") == 0)
				continue;

			Paths::setHomePath(argv[i + 1]);
			break;
		}
	}

	for(int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--videoduration") == 0)
		{
			gPlayVideoDuration = atoi(argv[i + 1]);
			i++; // skip the argument value
		}
		else if (strcmp(argv[i], "--video") == 0)
		{
			gPlayVideo = argv[i + 1];
			i++; // skip the argument value
		}
		else if (strcmp(argv[i], "--monitor") == 0)
		{
			if (i >= argc - 1)
			{
				std::cerr << "Invalid monitor supplied.";
				return false;
			}

			int monitorId = atoi(argv[i + 1]);
			i++; // skip the argument value
			Settings::getInstance()->setInt("MonitorID", monitorId);
		}
		else if(strcmp(argv[i], "--resolution") == 0)
		{
			if(i >= argc - 2)
			{
				std::cerr << "Invalid resolution supplied.";
				return false;
			}

			int width = atoi(argv[i + 1]);
			int height = atoi(argv[i + 2]);
			i += 2; // skip the argument value
			Settings::getInstance()->setInt("WindowWidth", width);
			Settings::getInstance()->setInt("WindowHeight", height);
			Settings::getInstance()->setBool("FullscreenBorderless", false);
		}else if(strcmp(argv[i], "--screensize") == 0)
		{
			if(i >= argc - 2)
			{
				std::cerr << "Invalid screensize supplied.";
				return false;
			}

			int width = atoi(argv[i + 1]);
			int height = atoi(argv[i + 2]);
			i += 2; // skip the argument value
			Settings::getInstance()->setInt("ScreenWidth", width);
			Settings::getInstance()->setInt("ScreenHeight", height);
		}else if(strcmp(argv[i], "--screenoffset") == 0)
		{
			if(i >= argc - 2)
			{
				std::cerr << "Invalid screenoffset supplied.";
				return false;
			}

			int x = atoi(argv[i + 1]);
			int y = atoi(argv[i + 2]);
			i += 2; // skip the argument value
			Settings::getInstance()->setInt("ScreenOffsetX", x);
			Settings::getInstance()->setInt("ScreenOffsetY", y);
		}else if (strcmp(argv[i], "--screenrotate") == 0)
		{
			if (i >= argc - 1)
			{
				std::cerr << "Invalid screenrotate supplied.";
				return false;
			}

			int rotate = atoi(argv[i + 1]);
			++i; // skip the argument value
			Settings::getInstance()->setInt("ScreenRotate", rotate);
		}else if(strcmp(argv[i], "--gamelist-only") == 0)
		{
			Settings::getInstance()->setBool("ParseGamelistOnly", true);
		}else if(strcmp(argv[i], "--ignore-gamelist") == 0)
		{
			Settings::getInstance()->setBool("IgnoreGamelist", true);
		}else if(strcmp(argv[i], "--show-hidden-files") == 0)
		{
			Settings::setShowHiddenFiles(true);
		}else if(strcmp(argv[i], "--draw-framerate") == 0)
		{
			Settings::getInstance()->setBool("DrawFramerate", true);
		}else if(strcmp(argv[i], "--no-exit") == 0)
		{
			Settings::getInstance()->setBool("ShowExit", false);
		}else if(strcmp(argv[i], "--exit-on-reboot-required") == 0)
		{
			Settings::getInstance()->setBool("ExitOnRebootRequired", true);
		}else if(strcmp(argv[i], "--no-startup-game") == 0)
		{
		        enable_startup_game = false;
		}else if(strcmp(argv[i], "--no-splash") == 0)
		{
			Settings::getInstance()->setBool("SplashScreen", false);
		}else if(strcmp(argv[i], "--splash-image") == 0)
		{
		        if (i >= argc - 1)
			{
				std::cerr << "Invalid splash image supplied.";
				return false;
			}
			Settings::getInstance()->setString("AlternateSplashScreen", argv[i+1]);
			++i; // skip the argument value
		}else if(strcmp(argv[i], "--debug") == 0)
		{
			Settings::getInstance()->setBool("Debug", true);
			Settings::getInstance()->setBool("HideConsole", false);
		}
		else if (strcmp(argv[i], "--fullscreen-borderless") == 0)
		{
			Settings::getInstance()->setBool("FullscreenBorderless", true);
		}
		else if (strcmp(argv[i], "--fullscreen") == 0)
		{
		Settings::getInstance()->setBool("FullscreenBorderless", false);
		}
		else if(strcmp(argv[i], "--windowed") == 0)
		{
			Settings::getInstance()->setBool("Windowed", true);
		}else if(strcmp(argv[i], "--vsync") == 0)
		{
			bool vsync = (strcmp(argv[i + 1], "on") == 0 || strcmp(argv[i + 1], "1") == 0) ? true : false;
			Settings::getInstance()->setBool("VSync", vsync);
			i++; // skip vsync value
		}else if(strcmp(argv[i], "--max-vram") == 0)
		{
			int maxVRAM = atoi(argv[i + 1]);
			Settings::getInstance()->setInt("MaxVRAM", maxVRAM);
		}
		else if (strcmp(argv[i], "--anti-aliasing") == 0)
		{
			if (i >= argc - 1)
			{
				std::cerr << "Invalid anti-aliasing supplied.";
				return false;
			}

			int antiAliasing = atoi(argv[i + 1]);
			if (antiAliasing != 0 && antiAliasing != 2 && antiAliasing != 4)
			{
				std::cerr << "Invalid anti-aliasing supplied.";
				return false;
			}

			Settings::getInstance()->setInt("AntiAliasing", antiAliasing);
			i++;
		}
		else if (strcmp(argv[i], "--force-kiosk") == 0)
		{
			Settings::getInstance()->setBool("ForceKiosk", true);
		}
		else if (strcmp(argv[i], "--force-kid") == 0)
		{
			Settings::getInstance()->setBool("ForceKid", true);
		}
		else if (strcmp(argv[i], "--force-disable-filters") == 0)
		{
			Settings::getInstance()->setBool("ForceDisableFilters", true);
		}
		else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
		{
#ifdef WIN32
			// This is a bit of a hack, but otherwise output will go to nowhere
			// when the application is compiled with the "WINDOWS" subsystem (which we usually are).
			// If you're an experienced Windows programmer and know how to do this
			// the right way, please submit a pull request!
			AttachConsole(ATTACH_PARENT_PROCESS);
			freopen("CONOUT$", "wb", stdout);
#endif
			std::cout <<
				"EmulationStation, a graphical front-end for ROM browsing.\n"
				"Written by Alec \"Aloshi\" Lofquist.\n"
				"Version " << PROGRAM_VERSION_STRING << ", built " << PROGRAM_BUILT_STRING << "\n\n"
				"Command line arguments:\n"
				"--resolution [width] [height]	try and force a particular resolution\n"
				"--gamelist-only			skip automatic game search, only read from gamelist.xml\n"
				"--ignore-gamelist		ignore the gamelist (useful for troubleshooting)\n"
				"--draw-framerate		display the framerate\n"
				"--no-exit			don't show the exit option in the menu\n"
				"--no-splash			don't show the splash screen\n"
				"--debug				more logging, show console on Windows\n"				
				"--windowed			not fullscreen, should be used with --resolution\n"
				"--vsync [1/on or 0/off]		turn vsync on or off (default is on)\n"
				"--max-vram [size]		Max VRAM to use in Mb before swapping. 0 for unlimited\n"
				"--anti-aliasing [0, 2 or 4]	set MSAA anti-aliasing to disabled, 2x or 4x\n"
				"--force-kid		Force the UI mode to be Kid\n"
				"--force-kiosk		Force the UI mode to be Kiosk\n"
				"--force-disable-filters		Force the UI to ignore applied filters in gamelist\n"
				"--home [path]		Directory to use as home path\n"
				"--help, -h			summon a sentient, angry tuba\n\n"
				"--monitor [index]			monitor index\n\n"				
				"More information available in README.md.\n";
			return false; //exit after printing help
		}
	}

	return true;
}

bool verifyHomeFolderExists()
{
	//make sure the config directory exists	
	std::string configDir = Paths::getUserEmulationStationPath();
	if(!Utils::FileSystem::exists(configDir))
	{
		std::cout << "Creating config directory \"" << configDir << "\"\n";
		Utils::FileSystem::createDirectory(configDir);
		if(!Utils::FileSystem::exists(configDir))
		{
			std::cerr << "Config directory could not be created!\n";
			return false;
		}
	}

	return true;
}

// Returns true if everything is OK,
bool loadSystemConfigFile(Window* window, const char** errorString)
{
	*errorString = NULL;

	StopWatch stopWatch("loadSystemConfigFile :", LogDebug);

	if(!SystemData::loadConfig(window))
	{
		LOG(LogError) << "Error while parsing systems configuration file!";
		*errorString = "IT LOOKS LIKE YOUR SYSTEMS CONFIGURATION FILE HAS NOT BEEN SET UP OR IS INVALID. YOU'LL NEED TO DO THIS BY HAND, UNFORTUNATELY.\n\n"
			"VISIT EMULATIONSTATION.ORG FOR MORE INFORMATION.";
		return false;
	}

	if(SystemData::sSystemVector.size() == 0)
	{
		LOG(LogError) << "No systems found! Does at least one system have a game present? (check that extensions match!)\n(Also, make sure you've updated your es_systems.cfg for XML!)";
		*errorString = "WE CAN'T FIND ANY SYSTEMS!\n"
			"CHECK THAT YOUR PATHS ARE CORRECT IN THE SYSTEMS CONFIGURATION FILE, "
			"AND YOUR GAME DIRECTORY HAS AT LEAST ONE GAME WITH THE CORRECT EXTENSION.\n\n"
			"VISIT EMULATIONSTATION.ORG FOR MORE INFORMATION.";
		return false;
	}

	return true;
}

//called on exit, assuming we get far enough to have the log initialized
void onExit()
{
	Log::close();
}

#ifdef WIN32
#define PATH_MAX MAX_PATH
#include <direct.h>
#endif

int setLocale(char * argv1)
{
#if WIN32
	std::locale::global(std::locale("en-US"));
#elif defined(ES_BUILTIN_TRANSLATIONS)
	// ES reads its translations itself (LocaleES.cpp), in the language of its settings
#else
	if (Utils::FileSystem::exists("./locale/lang")) // for local builds
		EsLocale::init("", "./locale/lang");	
	else
		EsLocale::init("", "/usr/share/locale");	
#endif

	setlocale(LC_TIME, "");

	return 0;
}


#if defined(MISTER) && defined(__linux__)
#include <ucontext.h>
// A crash's place, on standard error (es-mister.log): the signal, the address it touched, and the
// instruction's and its caller's addresses, put back to function names with the unstripped binary
// (llvm-symbolizer); then the signal does what it would have.
static void crashReport(int signum, siginfo_t* info, void* context)
{
	char text[256];
	const ucontext_t* uc = (const ucontext_t*)context;
#if defined(__arm__)
	const unsigned long pc = uc->uc_mcontext.arm_pc, lr = uc->uc_mcontext.arm_lr, sp = uc->uc_mcontext.arm_sp;
#else
	const unsigned long pc = 0, lr = 0, sp = 0;
	(void)uc;
#endif
	const int length = snprintf(text, sizeof(text), "ES crashed: signal %d (code %d) at address %p, pc 0x%08lx, lr 0x%08lx, sp 0x%08lx, thread %ld\n",
		signum, info ? info->si_code : 0, info ? info->si_addr : nullptr, pc, lr, sp, (long)syscall(SYS_gettid));
	if (length > 0)
		write(2, text, (size_t)length);
	signal(signum, SIG_DFL);
	raise(signum);
}

static void reportCrashes()
{
	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_sigaction = crashReport;
	action.sa_flags = SA_SIGINFO | SA_RESETHAND;
	sigemptyset(&action.sa_mask);
	for (int signum : { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT })
		sigaction(signum, &action, nullptr);
}
#endif

void signalHandler(int signum) 
{
	if (signum == SIGSEGV)
		LOG(LogError) << "Interrupt signal SIGSEGV received.\n";
	else if (signum == SIGFPE)
		LOG(LogError) << "Interrupt signal SIGFPE received.\n";
	else if (signum == SIGFPE)
		LOG(LogError) << "Interrupt signal SIGFPE received.\n";
	else
		LOG(LogError) << "Interrupt signal (" << signum << ") received.\n";

	Log::flush();

	// cleanup and close up stuff here  
	exit(signum);
}

void playVideo()
{
	ApiSystem::getInstance()->setReadyFlag(false);
	Settings::getInstance()->setBool("AlwaysOnTop", true);

	Window window;
	if (!window.init(true))
	{
		LOG(LogError) << "Window failed to initialize!";
		return;
	}

	Settings::getInstance()->setBool("VideoAudio", true);

	bool exitLoop = false;

	VideoVlcComponent vid(&window);
	vid.setVideo(gPlayVideo);
	vid.setOrigin(0.5f, 0.5f);
	vid.setPosition(Renderer::getScreenWidth() / 2.0f, Renderer::getScreenHeight() / 2.0f);
	vid.setMaxSize(Renderer::getScreenWidth(), Renderer::getScreenHeight());

	vid.setOnVideoEnded([&exitLoop]()
	{
		exitLoop = true;
		return false;
	});

	window.pushGui(&vid);

	vid.onShow();
	vid.topWindow(true);

	int lastTime = SDL_GetTicks();
	int totalTime = 0;

	while (!exitLoop)
	{
		SDL_Event event;

		if (SDL_PollEvent(&event))
		{
			do
			{
				if (event.type == SDL_QUIT)
					return;
			} 
			while (SDL_PollEvent(&event));
		}

		int curTime = SDL_GetTicks();
		int deltaTime = curTime - lastTime;

		if (vid.isPlaying())
		{
			totalTime += deltaTime;

			if (gPlayVideoDuration > 0 && totalTime > gPlayVideoDuration * 100)
				break;
		}

		Transform4x4f transform = Transform4x4f::Identity();
		vid.update(deltaTime);
		vid.render(transform);

		Renderer::swapBuffers();

		if (ApiSystem::getInstance()->isReadyFlagSet())
			break;
	}

	window.deinit(true);
}

void launchStartupGame()
{
	auto gamePath = SystemConf::getInstance()->get("global.bootgame.path");
	if (gamePath.empty() || !Utils::FileSystem::exists(gamePath))
		return;
	
	auto command = SystemConf::getInstance()->get("global.bootgame.cmd");
	if (!command.empty())
	{
		InputManager::getInstance()->init();
		command = Utils::String::replace(command, "%CONTROLLERSCONFIG%", InputManager::getInstance()->configureEmulators());
		Utils::Platform::ProcessStartInfo(command).run();		
	}	
}

// #include "utils/MathExpr.h"

#if defined(MISTER) && defined(__linux__)
// A script started from the MiSTer menu inherits Main_MiSTer's affinity: the second processor only.
// ES would then run on one processor, and believe the machine has one (musl counts the processors
// from the affinity), loading its systems one after another on the main thread. Allow them all.
static void useAllProcessors()
{
	cpu_set_t before;
	CPU_ZERO(&before);
	sched_getaffinity(0, sizeof(before), &before);

	const long count = sysconf(_SC_NPROCESSORS_CONF);
	cpu_set_t all;
	CPU_ZERO(&all);
	for (long i = 0; i < count && i < CPU_SETSIZE; i++)
		CPU_SET(i, &all);

	// /proc/cpuinfo when sysconf only sees the affinity
	if (CPU_COUNT(&all) <= 1)
	{
		if (FILE* f = fopen("/proc/cpuinfo", "r"))
		{
			char line[256];
			int found = 0;
			while (fgets(line, sizeof(line), f) != nullptr)
				if (strncmp(line, "processor", 9) == 0 && found < CPU_SETSIZE)
					CPU_SET(found++, &all);
			fclose(f);
		}
	}

	if (CPU_EQUAL(&before, &all))
		return;

	if (sched_setaffinity(0, sizeof(all), &all) == 0)
	{
		LOG(LogInfo) << "Processors: " << CPU_COUNT(&before) << " allowed, now " << CPU_COUNT(&all) << " (threads see " << std::thread::hardware_concurrency() << ")";
		StartupTrace::mark("processors", std::to_string(CPU_COUNT(&before)) + " -> " + std::to_string(CPU_COUNT(&all)));
	}
	else
		LOG(LogWarning) << "Processors: could not allow all " << CPU_COUNT(&all);
}
#endif

int main(int argc, char* argv[])
{
	// Utils::MathExpr::performUnitTests();

#ifdef WIN32
	// Must run before any window/message-queue APIs are touched.
	Utils::Platform::setDpiAwareness();
#endif

	// signal(SIGABRT, signalHandler);
	signal(SIGFPE, signalHandler);
	signal(SIGILL, signalHandler);
	signal(SIGINT, signalHandler);
	signal(SIGSEGV, signalHandler);
	// signal(SIGTERM, signalHandler);

	srand((unsigned int)time(NULL));

	std::locale::global(std::locale("C"));

	if(!parseArgs(argc, argv))
		return 0;

	// only show the console on Windows if HideConsole is false
#ifdef WIN32
	// MSVC has a "SubSystem" option, with two primary options: "WINDOWS" and "CONSOLE".
	// In "WINDOWS" mode, no console is automatically created for us.  This is good,
	// because we can choose to only create the console window if the user explicitly
	// asks for it, preventing it from flashing open and then closing.
	// In "CONSOLE" mode, a console is always automatically created for us before we
	// enter main. In this case, we can only hide the console after the fact, which
	// will leave a brief flash.
	// TL;DR: You should compile ES under the "WINDOWS" subsystem.
	// I have no idea how this works with non-MSVC compilers.
	if(!Settings::getInstance()->getBool("HideConsole"))
	{
		// we want to show the console
		// if we're compiled in "CONSOLE" mode, this is already done.
		// if we're compiled in "WINDOWS" mode, no console is created for us automatically;
		// the user asked for one, so make one and then hook stdin/stdout/sterr up to it
		if(AllocConsole()) // should only pass in "WINDOWS" mode
		{
			freopen("CONIN$", "r", stdin);
			freopen("CONOUT$", "wb", stdout);
			freopen("CONOUT$", "wb", stderr);
		}
	}else{
		// we want to hide the console
		// if we're compiled with the "WINDOWS" subsystem, this is already done.
		// if we're compiled with the "CONSOLE" subsystem, a console is already created;
		// it'll flash open, but we hide it nearly immediately
		if(GetConsoleWindow()) // should only pass in "CONSOLE" mode
			ShowWindow(GetConsoleWindow(), SW_HIDE);
	}
#endif

	// call this ONLY when linking with FreeImage as a static library
#ifdef FREEIMAGE_LIB
	FreeImage_Initialise();
#endif

	//if ~/.emulationstation doesn't exist and cannot be created, bail
	if(!verifyHomeFolderExists())
		return 1;

	if (!gPlayVideo.empty())
	{
		playVideo();
		return 0;
	}

	//start the logger
	Log::init();	
	StartupTrace::mark("ES main");

#if defined(MISTER) && defined(__linux__)
	useAllProcessors();
	reportCrashes();
#endif
	SampleProfiler::start();

	LOG(LogInfo) << "EmulationStation - v" << PROGRAM_VERSION_STRING << ", built " << PROGRAM_BUILT_STRING;

	//always close the log on exit
	atexit(&onExit);

	// Set locale
	setLocale(argv[0]);	

#if !WIN32
	if(enable_startup_game) {
	  // Run boot game, before Window Create for linux
	  launchStartupGame();
	}
#endif

	// Threaded initializations
	auto threadPool = new Utils::ThreadPool("main()", -3);
	auto vlcInit = threadPool->queueWorkItem([] { StartupTrace::Scope trace("video init"); VideoVlcComponent::init(); });
	threadPool->queueWorkItem([] { StartupTrace::Scope trace("ip address"); ApiSystem::getInstance()->getIpAddress(); });
	threadPool->queueWorkItem([] { StartupTrace::Scope trace("metadata init"); MetaDataList::initMetadata(); });
	threadPool->queueWorkItem([] { StartupTrace::Scope trace("mame names"); MameNames::init(); });
	threadPool->queueWorkItem([] { StartupTrace::Scope trace("genres"); Genres::init(); });
	threadPool->queueWorkItem([] { StartupTrace::Scope trace("http cookies"); HttpReq::resetCookies(); });
	threadPool->start();

	Window window;
	ViewController::init(&window);

	window.setReloadGamelistsCallback([&window] { ViewController::reloadAllGames(&window, true, true); });	
	window.pushGui(ViewController::get());
	bool windowReady;
	{
		StartupTrace::Scope trace("window init");
		windowReady = window.init(true, false);
	}
	if (!windowReady)
	{
		LOG(LogError) << "Window failed to initialize!";
		return 1;
	}

	Renderer::setWindowResizable(false);

	bool splashScreen = Settings::getInstance()->getBool("SplashScreen");
	bool splashScreenProgress = Settings::getInstance()->getBool("SplashScreenProgress");

	if (splashScreen)
		window.renderSplashScreen(splashScreenProgress ? _("Loading system config...") : _("Loading..."));

	Scripting::fireEvent("start");

	SystemScreenSaver screensaver(&window);
	{
		StartupTrace::Scope trace("collections init");
		CollectionSystemManager::init(&window);
	}
	
	Zaparoo::checkZaparooEnabledAsync();
	PowerSaver::init();
	InputConfig::AssignActionButtons();

	if (ApiSystem::getInstance()->isScriptingSupported(ApiSystem::PDFEXTRACTION))
		TextureData::PdfHandler = ApiSystem::getInstance();
	
	{
		StartupTrace::Scope trace("wait for init threads");
		threadPool->waitAllExcept(vlcInit); // Wait for what's necessary for loadSystemConfigFile
	}

	const char* errorMsg = NULL;
	bool systemsLoaded;
	{
		StartupTrace::Scope trace("load systems");
		systemsLoaded = loadSystemConfigFile(splashScreen && splashScreenProgress ? &window : nullptr, &errorMsg);
	}
	if (StartupTrace::enabled())
	{
		// what ES shows: each system with its games, and the ones it keeps out of sight
		std::string shown, hidden;
		for (auto sys : SystemData::sSystemVector)
		{
			const std::string entry = sys->getName() + " " + std::to_string(sys->getGameCountInfo()->totalGames);
			std::string& list = sys->isVisible() ? shown : hidden;
			list += (list.empty() ? "" : ", ") + entry;
		}
		StartupTrace::mark("systems shown", shown);
		StartupTrace::mark("systems not shown", hidden.empty() ? "-" : hidden);
	}

	if (!systemsLoaded)
	{
		// something went terribly wrong
		if (errorMsg == NULL)
		{
			LOG(LogError) << "Unknown error occured while parsing system config file.";
			Renderer::deinit();
			return 1;
		}

		// we can't handle es_systems.cfg file problems inside ES itself, so display the error message then quit
		window.pushGui(new GuiMsgBox(&window, errorMsg, _("QUIT"), [] { Utils::Platform::quitES(); }));
	}

	SystemConf* systemConf = SystemConf::getInstance();

#ifdef _ENABLE_KODI_
	if (systemConf->getBool("kodi.enabled", true) && systemConf->getBool("kodi.atstartup"))
	{
		if (splashScreen)
			window.closeSplashScreen();

		ApiSystem::getInstance()->launchKodi(&window);

		if (splashScreen)
		{
			window.renderSplashScreen("");
			splashScreen = false;
		}
	}
#endif

	// preload what we can right away instead of waiting for the user to select it
	// this makes for no delays when accessing content, but a longer startup time
	{
		StartupTrace::Scope trace("preload views");
		ViewController::get()->preload();
	}

#ifdef MISTER
	// started by es-mister.sh before the screen was ready (ES_SOFT_OUTPUT_WAIT), ES has loaded meanwhile: the
	// controllers are opened once the screen is ES's
	{
		StartupTrace::Scope trace("wait for the screen");
		Renderer::waitForScreen();
	}
#endif

	// Initialize input
	{
		StartupTrace::Scope trace("input init");
		InputManager::getInstance()->init();
	}
	SDL_StopTextInput();

	NetworkThread* nthread = new NetworkThread(&window);
	HttpServerThread httpServer(&window);

	// tts
	TextToSpeech::getInstance()->enable(Settings::getInstance()->getBool("TTS"), false);
	
	if (errorMsg == NULL)
	{
		if (splashScreen)
			window.renderSplashScreen(_("Loading theme"));

		StartupTrace::Scope trace("go to start");
		ViewController::get()->goToStart(true);
	}

	{
		StartupTrace::Scope trace("wait for init threads");
		threadPool->wait();
	}
	delete threadPool;

	window.closeSplashScreen();

	// Create a flag in  temporary directory to signal READY state
	ApiSystem::getInstance()->setReadyFlag();

	// Play music
	{
		StartupTrace::Scope trace("audio init");
		AudioManager::getInstance()->init();
	}
	StartupTrace::finish();

	if (ViewController::get()->getState().viewing == ViewController::GAME_LIST || ViewController::get()->getState().viewing == ViewController::SYSTEM_SELECT)
		AudioManager::getInstance()->changePlaylist(ViewController::get()->getState().getSystem()->getTheme());
	else
		AudioManager::getInstance()->playRandomMusic();


#ifdef WIN32	
	DWORD displayFrequency = 60;

	DEVMODE lpDevMode;
	memset(&lpDevMode, 0, sizeof(DEVMODE));
	lpDevMode.dmSize = sizeof(DEVMODE);
	lpDevMode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFLAGS | DM_DISPLAYFREQUENCY;
	lpDevMode.dmDriverExtra = 0;

	if (EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &lpDevMode) != 0) {
		displayFrequency = lpDevMode.dmDisplayFrequency; // default value if cannot retrieve from user settings.
	}

	int timeLimit = (1000 / displayFrequency) - 10;	 // Margin for vsync
	if (timeLimit < 0)
		timeLimit = 0;
#endif

	Renderer::setWindowResizable(true);

	int lastTime = SDL_GetTicks();
	int ps_time = SDL_GetTicks();

	bool running = true;

#ifdef BATOCERA
	bool hotkeyPressed = false;
#endif

	while(running)
	{
		SDL_Event event;
#ifdef MISTER_PERF
		const int64_t perfStart = Perf::nowNs();
		int64_t perfWaited = 0;
#endif

		int screenSaverTimeout = screensaver.getNextUpdateTimeout();
		bool screenSaverWait = screenSaverTimeout > 0;

		bool ps_standby =
			!screenSaverWait &&
			PowerSaver::getState() &&
			(int)SDL_GetTicks() - ps_time > PowerSaver::getMode();

		int eventResult;

		if (screenSaverWait)
			eventResult = SDL_WaitEventTimeout(&event, screenSaverTimeout);
		else if (ps_standby)
			eventResult = SDL_WaitEventTimeout(&event, PowerSaver::getTimeout());
		else
			eventResult = SDL_PollEvent(&event);
#ifdef MISTER_PERF
		perfWaited = Perf::nowNs();
#endif

		if (eventResult)
		{
			// PowerSaver can push events to exit SDL_WaitEventTimeout immediatly
			// Reset this event's state
			TRYCATCH("resetRefreshEvent", PowerSaver::resetRefreshEvent());

			do
			{
#ifdef BATOCERA
			  // global hotkeys
			  bool eventTaken = false;
			  if(event.type == SDL_JOYBUTTONDOWN || event.type == SDL_JOYBUTTONUP)
			    {
			      InputConfig* config = InputManager::getInstance()->getInputConfigByDevice(event.jbutton.which);
			      if(config)
				{
				  // Find first player controller info
				  auto playerDevices = InputManager::getInstance()->lastKnownPlayersDeviceIndexes();
				  auto playerDevice = playerDevices.find(0);
				  if (playerDevice != playerDevices.cend())
				    {
				      if (config->getDeviceIndex() == playerDevice->second.index)
					{
					  Input input = Input(event.jbutton.which, TYPE_BUTTON, event.jbutton.button, event.jbutton.state == SDL_PRESSED, false);
					  if (config->isMappedTo("hotkey", input))
					    hotkeyPressed = input.value != 0;

					  if(hotkeyPressed && input.value != 0)
					    {
					      std::string hotkey_controlcenter = Settings::getInstance()->getString("HOTKEY_CONTROLCENTER");
					      if (config->isMappedTo(hotkey_controlcenter, input))
						{
						  hotkeyPressed = false;
						  ApiSystem::getInstance()->launchControlcenter();
						  eventTaken = true;
						}
					    }
					}
				    }
				}
			    }
			  //
			  if(eventTaken)
			    continue;
#endif

				// a press of the user's: the renderer measures the time until a frame shows its effect
				if ((event.type == SDL_KEYDOWN && !event.key.repeat) || event.type == SDL_JOYBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONDOWN ||
					(event.type == SDL_JOYHATMOTION && event.jhat.value != SDL_HAT_CENTERED) ||
					(event.type == SDL_JOYAXISMOTION && (event.jaxis.value > 23000 || event.jaxis.value < -23000)))
					StartupTrace::inputArrived();

				TRYCATCH("InputManager::parseEvent", InputManager::getInstance()->parseEvent(event, &window));

				if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED && Settings::getInstance()->getBool("Windowed"))
				{
					if (Renderer::onScreenSizeChanged(event.window.data1, event.window.data2))
					{
						Renderer::setWindowResizable(false);

						window.closeSplashScreen();

						while (window.peekGui() && window.peekGui() != ViewController::get())
							delete window.peekGui();

						ViewController::get()->reloadAll(&window);
						window.closeSplashScreen();

						Renderer::setWindowResizable(true);
					}
				}				

				if (event.type == SDL_QUIT)
					running = false;
			} 
			while(SDL_PollEvent(&event));

			// check guns
			InputManager::getInstance()->updateGuns(&window);

			// triggered if exiting from SDL_WaitEvent due to event
			if (ps_standby || (screenSaverWait && !screensaver.isScreenSaverActive()))
				// show as if continuing from last event
				lastTime = SDL_GetTicks();

			// reset counter
			ps_time = SDL_GetTicks();
		}
		else if (ps_standby == false)
		{
		  // check guns
		  InputManager::getInstance()->updateGuns(&window);

		  if (screenSaverWait)
			ps_time = SDL_GetTicks();

		  // If exitting SDL_WaitEventTimeout due to timeout. Trail considering
		  // timeout as an event
		  //	ps_time = SDL_GetTicks();
		}

		if (window.isSleeping())
		{
			lastTime = SDL_GetTicks();
			SDL_Delay(1); // this doesn't need to be accurate, we're just giving up our CPU time until something wakes us up
			continue;
		}

		int curTime = SDL_GetTicks();
		int deltaTime = curTime - lastTime;
		lastTime = curTime;

		// cap deltaTime if it ever goes negative
		if(deltaTime < 0)
			deltaTime = 1000;

#ifdef MISTER
		// A frame slowed by a large picture loading counts for at most 100 ms: lists repeat a held button
		// after 500 ms of frame time, and one slow frame made a single press move two places (the system
		// after the next was shown, the next one never).
		if (deltaTime > 100)
			deltaTime = 100;
#endif
#ifdef MISTER_PERF
		// golden frames: every turn 1/60 s of ES's time, the same frames on every run
		if (Perf::fixedStep())
			deltaTime = Perf::nextStep();
		const int64_t perfEvents = Perf::nowNs();
#endif

		TRYCATCH("Window.update" ,window.update(deltaTime))	
#ifdef MISTER_PERF
		const int64_t perfUpdated = Perf::nowNs();
#endif
		TRYCATCH("Window.render", window.render())
#ifdef MISTER_PERF
		const int64_t perfRendered = Perf::nowNs();
#endif

		int fpsLimit = Settings::FpsLimit();
		if (fpsLimit > 0)
		{
			int frameTime = (1000 + fpsLimit / 2) / fpsLimit;
			int processDuration = SDL_GetTicks() - curTime;
			if (processDuration < frameTime)
			{
				int timeToWait = frameTime - processDuration;
				if (timeToWait > 0 && timeToWait < 100)
					SDL_Delay(timeToWait);
			}
		}

#ifdef MISTER_PERF
		const int64_t perfPaced = Perf::nowNs();
#endif
		Renderer::swapBuffers();		
#ifdef MISTER_PERF
		Perf::loopTurn(perfStart, perfWaited, perfEvents, perfUpdated, perfRendered, perfPaced, Perf::nowNs());
#endif
	}
#ifdef MISTER_PERF
	Perf::flush();
#endif

	if (Utils::Platform::isFastShutdown())
		Settings::getInstance()->setBool("IgnoreGamelist", true);

	WatchersManager::stop();
	ThreadedHasher::stop();
	ThreadedScraper::stop();

	ApiSystem::getInstance()->deinit();

	while (window.peekGui() != ViewController::get())
		delete window.peekGui();

	if (SystemData::hasDirtySystems())
		window.renderSplashScreen(_("SAVING METADATA. PLEASE WAIT..."));

	MameNames::deinit();
	ViewController::saveState();
	CollectionSystemManager::deinit();
	SystemData::deleteSystems();
	Scripting::exitScriptingEngine();

	// call this ONLY when linking with FreeImage as a static library
#ifdef FREEIMAGE_LIB
	FreeImage_DeInitialise();
#endif
	
	// Delete ViewController
	while (window.peekGui() != nullptr)
		delete window.peekGui();

	window.deinit();

	Utils::Platform::processQuitMode();

	LOG(LogInfo) << "EmulationStation cleanly shutting down.";

	Log::flush();

	return 0;
}

