__ES_TITLE__ - Batocera's EmulationStation on the MiSTer FPGA
=====================================================================

What it is
----------
Batocera's frontend (EmulationStation, with the Carbon theme) running on the
MiSTer's ARM, drawn in software (the MiSTer has no GPU) on the Linux
framebuffer, which the MiSTer shows on the TV (drawn at 1280x720, its frames
are enlarged by EmulationStation itself to fill a 1080p screen: FILL below).
Pick a game, play it on its MiSTer core, and come back to the frontend.

This package draws at __ES_SIZE__ (SIZE in es-mister.ini).

Installing
----------
1. Extract the ZIP at the root of the MiSTer's SD card. You get:
     /media/fat/__ES_DIR__/          (the program, theme and settings)
     /media/fat/Scripts/__ES_SCRIPT__.sh
   Updating: extract it over the old one. Games, covers and EmulationStation's
   own settings stay; __ES_DIR__/es-mister.ini is replaced with the default
   one (save yours first if you changed it).
   Updating from 0.10 beta 5 or earlier (the folder es-mister-720p): first
   rename /media/fat/es-mister-720p to /media/fat/__ES_DIR__ (EmulationStation's
   settings and caches stay), then extract, and delete the old launcher
   Scripts/es-mister-720p.sh.
2. Keep "fb_terminal=1" in MiSTer.ini (it is the default).
3. Games stay in the MiSTer's usual folders (games/NES, games/SNES,
   games/MegaDrive, games/GBA, games/PSX...; arcade in _Arcade; the Sharp
   X68000 as the MGL files in _Computer/_X68000 Games, each starting its game's
   disks as the MiSTer menu does). A system with two usual folders (games/GBC
   or games/GAMEBOY, games/GameGear or games/SMS, games/Atari2600 or
   games/ATARI7800) shows the first of them that exists. The systems and their
   folders are listed in __ES_DIR__/systems.tsv. Games inside .zip files are
   listed for the Virtual Boy (they usually come zipped), not yet elsewhere.

Space needed on the SD card
---------------------------
- The program, its theme and settings: about 90 MB (the ZIP itself: 58 MB).
- Covers, downloaded next to the games: about 420 KB a game on average
  (measured: 20,069 covers, 8.5 GB).
- The picture cache (cache/pictures, PICTURE_CACHE): about 200 KB for each
  cover seen once (measured: 16,912 covers, 3.3 GB). It is not written while
  less than 1 GB is free, and can be deleted at any time.
- The theme cache (cache/themes) and logs: about 10 MB.
In all, about 100 MB plus 0.6 GB for every 1,000 games with covers (1 GB free
left over keeps the picture cache working): 5,000 games need about 3.2 GB,
20,000 games about 12.5 GB.

Using it
--------
- In the MiSTer menu: Scripts > __ES_SCRIPT__. The MiSTer menu goes away, the
  logo shows at once (the one EmulationStation drew at its last start, kept in
  cache/splash.snapshot), and the systems come a few seconds later. The logo
  stays at least SPLASH_TIME seconds (es-mister.ini).
- Keyboard: arrows, Enter (choose), Esc (back), Space (menu).
  Controllers: the common ones are recognised; otherwise EmulationStation asks
  you to set the buttons up. On a controller, Select opens the menu, and Start
  opens a game list's options (on the systems: the quick Quit menu).
- The language is chosen in Menu > System settings > Language (English at first).
- Drawing times go to logs/es-mister-stats.txt every 600 frames.
  EmulationStation's own frame counter (Show framerate) works too, but it
  changes the screen every frame and so forces the whole screen to be drawn
  again each time: it shows the worst case, not normal use.
- To play: choose the game. Its MiSTer core loads with it, at once: the zoom
  on the game's picture before it is in Menu > User interface settings >
  Game launch transition.
- To come back from a game: hold Create and Options together for a second
  (Share and Options on a DualShock 4). Or open the OSD (F12 or the
  controller's menu button) and go back to the MiSTer menu (System > Reboot,
  a short press, not held). The MiSTer menu core loads again (about 2
  seconds) and EmulationStation comes back by itself: the MiSTer menu
  shows only for a moment (about 0.2 s).
- To leave EmulationStation: Menu > Quit > Quit EmulationStation (back to the
  MiSTer menu).
- Menu > Quit > Restart system: the MiSTer restarts cold, as its menu's
  "Reboot" held.
- Menu > Quit > Shutdown operating system: the MiSTer shuts down and its HDMI
  output goes off (the TV shows no signal). The DE10-Nano cannot cut its own
  power: its LEDs stay on; unplug it, or plug it in again to turn it on.
- The controller's button for the MiSTer menu (the DualSense's PS) asks
  first: yes closes EmulationStation and the MiSTer menu comes back.
- F12 on the keyboard shows the MiSTer menu over it: EmulationStation stays
  open, paused. To go back to it: F9, or Scripts > __ES_SCRIPT__ again.
  Meanwhile the menu's other scripts run as usual. After F12 the controller's
  button opens the MiSTer menu at once, without asking, until
  EmulationStation is shown again. Started from the Scripts menu, F12 does
  nothing until a game has been played (the MiSTer takes no keys while its
  script runs): the controller's button above (MENU_ASK=1) or Menu > Quit goes
  back to the MiSTer menu.
- Whenever the MiSTer shows a screen of its own over EmulationStation (its
  menu, or its background after the TV is turned on again and the HDMI link
  comes back), the controllers are its until EmulationStation is shown again;
  after the HDMI link comes back, EmulationStation is shown again by itself
  within a few seconds.

Game covers
-----------
A new install shows the games without covers: EmulationStation downloads them
itself, once, for the games on your SD card, whatever their file names (the
MiSTer needs the network, Wi-Fi or cable):
1. Start EmulationStation and stay on the systems screen (opened from a game
   list, the scraper offers only that list's system).
2. Open the menu (Select on a controller, Space on the keyboard) > Scraper.
3. Leave the defaults: Scrape from "Libretro-thumbnails" (no account, no
   key), Games to scrape for "Games missing any media", Systems included: all
   the systems with games.
4. Scrape now. It works in the background, its progress in a notice on the
   screen, and you can keep browsing. With thousands of games it takes a
   while: leave the MiSTer on until it ends (quitting EmulationStation
   meanwhile asks whether to stop it).
Scraping again later only fetches what is missing: new games, covers not found.

Where they come from: Menu > Scraper > Scrape from: "Libretro-thumbnails" (the
default). No account and no key needed. The pictures come from
libretro-thumbnails (the source the PS5's snes9x uses): box art, title screen
and screenshot; the box art is the game's picture by default (Image source:
Box 2D). A game is recognised by its ROM's CRC32 (No-Intro list) or by its
file name, "(JP)", "(US)", "(EU)" and hacks such as "- Fastrom" included. For
games libretro-thumbnails does not have (hacks, homebrew, unlicensed carts),
the cover comes from Hasheous (an IGDB picture) or TheGamesDB.
The pictures are saved next to the games: games/<SYSTEM>/images/, listed in
that folder's gamelist.xml. Each system's list of pictures is kept in
home/.emulationstation/scrapers/libretro (refreshed every 14 days).
To download covers again soon after a first scrape, set "Ignore recently
scraped games" to "No".
IGDB works too, with your own Twitch Client ID and Secret (Accounts tab).

Settings
--------
/media/fat/__ES_DIR__/es-mister.ini:
  SIZE=__ES_SIZE__   the size EmulationStation draws at: auto is the screen's
                 (1920x1080 on a 1080p TV), the sharpest and the slowest;
                 auto/2 is half of it, auto/3 a third, or WIDTHxHEIGHT
  FILL=1         1 = the picture fills the screen when SIZE is WIDTHxHEIGHT:
                 EmulationStation enlarges its frames to the screen's size
                 (1280x720 on a 1080p TV: 1.5 times); 0 = the MiSTer
                 enlarges them only by whole times (1280x720 stays in the
                 middle of a 1080p screen)
  MENU_ASK=1     1 = the controller's button for the MiSTer menu (the
                 DualSense's PS) asks first, and yes closes EmulationStation;
                 0 = it opens the MiSTer menu over it at once
  FPS=60         the most frames a second
  SPLASH_TIME=2.2  the least time, in seconds, the logo stays on screen when
                 EmulationStation is ready sooner; 0 = not held
  BENCH=0        1 = the whole screen drawn again every frame, to measure what
                 drawing costs (see logs/es-mister-stats.txt)
  STRIP=auto     rows in each band of the drawing: auto chooses by the screen's
                 width (24 at 1080p), or a number (0 = one layer at a time)
  PLD=256        bytes asked of the memory ahead of time (0 = off, to compare)
  CAPTURE=0      1 = slow frames also saved whole (logs/es-slow-frame-N.escf),
                 to measure with es-mister-replay
  PICTURE_CACHE=1  1 = pictures kept decoded at the size shown, without loss,
                 in cache/pictures (~200 KB a cover; not written while less
                 than 1 GB is free): a cover seen before comes back ~20 times
                 faster; 0 = none. The folder can be deleted at any time.
  THEME_CACHE=1  1 = each system's theme kept as the theme built it, in
                 cache/themes, and read back at the next starts while nothing
                 it depends on changed (a start ~2 s faster); built again by
                 itself after a theme setting, the language or
                 EmulationStation changes; 0 = none. The folder can be deleted
                 at any time.
  PREFETCH=1     1 = the games folders, gamelists, theme and EmulationStation
                 itself read in the background while the start waits for the
                 MiSTer menu (a faster first start after switching the MiSTer
                 on); only read, nothing written; 0 = not. Only with SIZE=auto
                 or FILL=0: otherwise EmulationStation starts at once and reads
                 them itself meanwhile
  STOP_SERVICES="bgm zaparoo arcsnd"
                 what else runs on the MiSTer, closed while EmulationStation
                 runs (so that it has the processor and the memory to itself)
                 and started again when it closes, as the MiSTer starts it at
                 boot: the MiSTer BGM (under EmulationStation its music
                 skipped), Zaparoo (NFC cards; "write an NFC tag" is not
                 offered meanwhile) and the arcade sound server (arcsnd:
                 paused while EmulationStation is on screen, running during
                 the games it starts, whose sound it plays). Take a name out
                 to leave it running. Can be added: samba (the SD card on the
                 network), ftp (the FTP server), ntp (the clock's network
                 time), syslog (the system log), gpm (the console's mouse).
                 Always left running: the MiSTer's main program, Wi-Fi and
                 the network, SSH, Bluetooth, udev, the consoles' logins and
                 the MiSTer's sync of the SD card every 5 s.

Measuring the drawing on your MiSTer
------------------------------------
es-mister-bench draws, without showing anything new, EmulationStation's
heaviest frame (the system change in the carousel, at 1920x1080) in several
ways and times each one on the MiSTer's ARM. Close EmulationStation (Menu >
Quit) and, over SSH, right after:
  /media/fat/__ES_DIR__/es-mister-bench | tee /media/fat/__ES_DIR__/logs/es-mister-bench.txt
It takes about 2 minutes.

Measuring with EmulationStation's own frames
--------------------------------------------
With CAPTURE=1 in es-mister.ini, slow frames are also saved whole
(logs/es-slow-frame-N.escf). Close EmulationStation and, over SSH:
  /media/fat/__ES_DIR__/es-mister-replay logs/es-slow-frame-*.escf
It draws each frame again and shows the drawing time and a code for the
pixels: two versions that give the same code drew the same picture.
With ES_SOFT_LAYERS=0 before the command, the large layers (the carousel's
backgrounds and what lies on them) are drawn one by one instead of together,
to compare the time; the pixels' code is the same.

What to send when reporting a problem
-------------------------------------
- /media/fat/__ES_DIR__/logs/es-mister.log
- /media/fat/__ES_DIR__/logs/es-mister-stats.txt   (the drawing times)
- /media/fat/__ES_DIR__/home/.emulationstation/es_log.txt
- What you saw: how smooth browsing was, wrong screens, controls, whether the
  game started and whether coming back worked.

If something hangs
------------------
- If EmulationStation's screen does not show up: started from the Scripts
  menu, the MiSTer takes no keys while its script runs, and if EmulationStation
  cannot start, the MiSTer menu comes back by itself; otherwise press F9 (shows
  Linux) or F12 (back to the MiSTer menu).
- To end it by force over SSH:  killall emulationstation
  es-mister.sh puts the console back in text mode and loads the MiSTer menu
  again by itself. If it was ended too, do it by hand:
    /media/fat/__ES_DIR__/es-mister-helper text
    echo "load_core /media/fat/menu.rbf" > /dev/MiSTer_cmd
  (without the first command, F9 and the menu's scripts stop working until the
  MiSTer restarts)

Limits of this version
----------------------
- No preview videos (the game's picture is shown instead).
- ScreenScraper and TheGamesDB (with search) need keys at build time.
- Games in .zip files (but the Virtual Boy's), and one system's games spread
  over several folders, not yet.
- Atari 5200: a 16 KB cart in .a52 or .bin fits two cart types, and the core
  asks which in its OSD, which a game started from EmulationStation cannot
  answer: the core waits at the Atari screen; load the cart from the OSD.
- Amiga CD32: the disc is written into the CD32's configuration
  (config/AmigaCD32.cfg) as Zaparoo does; start the CD32 once from the MiSTer
  menu first, so that file exists.
- No shader effects (blur etc.).

Licences: EmulationStation (MIT, LICENSE-EmulationStation.md); Carbon theme by
Fabrice Caruso (CC BY-NC-SA); controller database from batocera.linux.
