Batocera's EmulationStation on the MiSTer FPGA - test version (phase 1)
=====================================================================

What it is
----------
Batocera's frontend (EmulationStation, with the Carbon theme) running on the
MiSTer's ARM, drawn in software (the MiSTer has no GPU) on the Linux
framebuffer, which the FPGA scales up for the TV. This version is for measuring
whether the interface runs smoothly on the MiSTer and for testing the cycle
"choose a game -> play -> come back".

Installing
----------
1. Extract the ZIP at the root of the MiSTer's SD card. You get:
     /media/fat/emulationstation/          (the program, theme and settings)
     /media/fat/Scripts/emulationstation.sh
2. Keep "fb_terminal=1" in MiSTer.ini (it is the default).
3. Games stay in the MiSTer's usual folders (games/NES, games/SNES,
   games/MegaDrive, games/GBA, games/PSX...; arcade in _Arcade). Files inside
   .zip archives do not show up yet in this version.

Using it
--------
- In the MiSTer menu: Scripts > emulationstation. The MiSTer screen goes away
  and EmulationStation shows up within a few seconds.
- Keyboard: arrows, Enter (choose), Esc (back), Space (menu).
  Controllers: the common ones are recognised; otherwise EmulationStation asks
  you to set the buttons up.
- Drawing times go to logs/es-mister-stats.txt every 600 frames.
  EmulationStation's own FPS counter (interface settings) works too, but it
  changes the screen every frame and so forces everything to be redrawn every
  time: it shows the worst case, not normal use.
- To play: choose the game. Its MiSTer core loads with it.
- To come back from a game: open the OSD (F12 or the controller's menu button),
  go to the system page and choose "Reboot" with a short press (not held). The
  MiSTer menu reloads and EmulationStation comes back by itself within a few
  seconds.
- To leave EmulationStation: Menu > Quit > Quit EmulationStation.

Settings
--------
/media/fat/emulationstation/es-mister.ini:
  SIZE=auto      the framebuffer's native resolution; on a TV set to 1080p it
                 draws at 1920x1080, with no reduction of the picture
  FPS=60         the most frames a second
  BENCH=0        1 = the whole screen redrawn every frame, to measure what
                 drawing costs (see logs/es-mister-stats.txt)
  PROFILE=0      CPU diagnostics; leave off in normal use
  SLOW_DUMPS=0   diagnostics that redraw frames; leave off in normal use
  STRIP=8        draws in bands of eight rows to make good use of the cache
  PLD=256        memory prefetch in the NEON loops and the copy

FPS=60 is the limit, not a promise of 60 new frames a second. The Carbon theme
at 1080p may need more than 16.7 ms when the whole screen changes.
The helper recognises the framebuffer already active when reopened over SSH;
there is no need to log in on the console shown on the TV to start the
frontend.

What to send back after the test
--------------------------------
- /media/fat/emulationstation/logs/es-mister.log
- /media/fat/emulationstation/logs/es-mister-stats.txt   (the drawing times)
- /media/fat/emulationstation/home/.emulationstation/es_log.txt
- What you saw: how smooth browsing was, wrong screens, controls, whether the
  game started and whether coming back worked.

If something hangs
------------------
- If EmulationStation's screen does not show up, press F9 (shows Linux) or F12
  (back to the MiSTer menu).
- To end it by force over SSH:  killall emulationstation
  and then: echo "load_core /media/fat/menu.rbf" > /dev/MiSTer_cmd

Limits of this version
----------------------
- No preview videos (the game's picture is shown instead).
- No HTTPS: the scraper and theme downloads do not work yet.
- Games in .zip files, and several games folders for one system, not yet.
- No shader effects (blur etc.).

Licences: EmulationStation (MIT, LICENSE-EmulationStation.md); Carbon theme by
Fabrice Caruso (CC BY-NC-SA); controller database from batocera.linux.
