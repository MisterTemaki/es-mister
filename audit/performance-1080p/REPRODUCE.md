# Building and reproducing the final stage

The commands below start from the `es-mister-audit` folder, which contains `es-mister/`
and `validation/`. The bench recorded uses WSL Ubuntu-26.04 and Windows' OpenSSH.
The device's address and the authorised public key are in the scripts; the private
key is not distributed. Use new prefixes so as not to mix runs.

## Distributed build

On Linux, the official path is still `mister/build.sh`; the `es` variant
reuses dependencies already prepared, while `all` also prepares and packages them.
Zig 0.15.2, CMake, Ninja and the tools listed in the script's header are
needed. The ELF is static ARMv7/musl, Cortex-A9 with NEON.

```bash
cd es-mister
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install ziglang==0.15.2
export MISTER_WORK=/path/with-space
export JOBS=6
bash mister/build.sh deps
bash mister/build.sh es
```

The ELF ends up in `$MISTER_WORK/out/emulationstation`. The partial update uses
the Carbon theme already installed. `mister/build.sh all` also generates a full package,
including the theme preparation, and is not needed to update that ELF.

The session's cache is not part of the delivery. With it still available on this PC:

```powershell
wsl -d Ubuntu-26.04 -u root -- bash ./validation/cross_cache.sh bash ./validation/cross_build.sh repro-candidate active
wsl -d Ubuntu-26.04 -- arm-linux-gnueabihf-strip -o ./validation/arm/emulationstation-repro-candidate-stripped ./validation/arm/emulationstation-repro-candidate
wsl -d Ubuntu-26.04 -u root -- bash ./validation/cross_cache.sh bash ./validation/cross_build.sh repro-reference strict-reference
wsl -d Ubuntu-26.04 -- arm-linux-gnueabihf-strip -o ./validation/arm/emulationstation-repro-reference-stripped ./validation/arm/emulationstation-repro-reference
```

`strict-reference` restores the catalogue, resources, rasteriser and output sources
from the verified reference. The markers are common to both builds.
`tests/audit/reference/` allows rebuilding that reference without Git history.
The `*.source.json` manifest records each file prepared. Wait for the build to
finish before strip/upload. A new build may have a different hash;
record its real hash instead of reusing the previous session's.

## Bench on the device

The core must be MENU. Do not run two test controllers at the same time.
Each run uses an isolated HOME and the same configuration copied from the installation.
The process guard checks the executable in `/proc`, also waits for the previous
wrapper to exit and refuses a second instance. `restore` waits for the process, the
`ready` marker and the API, without assuming that creating the wrapper made the UI ready.

```powershell
wsl -d Ubuntu-26.04 -- python3 ./validation/target_run.py upload ./validation/arm/emulationstation-repro-reference-stripped stage3-final-reference
wsl -d Ubuntu-26.04 -- python3 ./validation/target_run.py upload ./validation/arm/emulationstation-repro-candidate-stripped stage3-final-candidate
```

Update the expected hashes in `final_navigation.ps1`, `final_startup.ps1` and
`combined_compare.py` to the newly built artefacts. The configuration measured
is 1920×1080, Carbon, two rendering threads, STRIP=8, PLD=256, FPS=60 and
OPTIMIZATIONS=1. The reference uses catalogue4/decoder2 with the new upload/fade/copy
off; the candidate uses catalogue2/decoder1 with all three on. Do not attribute to
the new change the historical gain against OPTIMIZATIONS=0.

```powershell
pwsh -NoProfile -File ./validation/final_navigation.ps1 -Prefix repro-nav
pwsh -NoProfile -File ./validation/final_state_compare.ps1 -Mode idle -Prefix repro-idle
pwsh -NoProfile -File ./validation/final_state_compare.ps1 -Mode controls -Prefix repro-controls
pwsh -NoProfile -File ./validation/final_animation_compare.ps1 -Prefix repro-animations
pwsh -NoProfile -File ./validation/final_startup.ps1 -Prefix repro-warm
pwsh -NoProfile -File ./validation/final_startup.ps1 -Prefix repro-cold -Reboot
```

The last command restarts Linux four times, confirms a new boot_id and MENU and
uses the same service stabilisation interval at each boot. It does not run
drop_caches nor read the ELF before the measured opening. The hash is checked afterwards.
The first key is injected when the UI reaches the interactive marker, with a
real selection change verified; no blind nine-second wait is used.

The scripts `cover_compare.ps1`, `install_cover_fixture.py` and `cover_analyze.py`
reproduce a list of 1,000 entries, 160 shared original PNGs and
round trips. The fixture does not change real games, covers or metadata. The fixtures'
paths need to be prepared on the new device before running.
`target_lifecycle.py core PREFIX` uses only `codex-video-probe.nes`, an
original test program without save RAM; it does not use a commercial ROM.

## Quality and tests on the host

The host needs a C++17 compiler, CMake, pkg-config and SDL2, CURL and pugixml
headers/libraries. In the session, pugixml came from the local prefix `validation/tools/usr`;
that dependency directory is not in the ZIP. On another host use the system's
development packages or give its prefix through CMAKE_PREFIX_PATH.

```bash
cmake -S es-mister/tests/audit -B validation/regressions -DAUDIT_SANITIZERS=ON
cmake --build validation/regressions -j2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./validation/regressions/audit_texture_manager
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./validation/regressions/audit_texture_manager ram
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./validation/regressions/audit_texture_manager ram-under-limit
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./validation/regressions/audit_regressions
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./validation/regressions/audit_thread_pool
python3 validation/build_upload.py
python3 validation/check_target_payload.py
python3 validation/check_lifecycle_payload.py
```

The raster/backend differentials use the independent previous implementation;
`tests/audit/raster_workload.cpp` includes clipping, alpha, stencil, colours and UV.
The specialised build/comparison scripts are in `validation/`; the logs
of the accepted run identify which tests ran on the host and on the ARM.
Prototypes in `tests/audit/experiments/` do not go into the distributed binary.

To prepare a new device, generate the deterministic pictures and install the
fixture only with the benchmarks stopped. These commands are not part of the
speed pass:

```bash
python3 validation/create_cover_fixture.py
python3 validation/install_cover_fixture.py
python3 validation/create_nes_probe.py
```

Speed and quality are different passes. During speed, do not
enable double checking, PNG, slow dumps, signal profiling or forced
redraw. A quality binary with a fixed logical clock only serves to compare
the same animation state; that code is not in the distributed main.cpp.

The 60 FPS limit does not prove 60 full redraws a second. SDL/uinput
times end at the first redraw written by the software; they do not measure the
controller's physical latency or the scanout/TV, nor the end of the whole transition.

In the core-return test, the controls are sent after a safety wait
and validated by five state/selection probes. That wait in the script is not in the
distributed launcher. The time to first drawing after the helper is measured before
it; the shortest input time after returning remains unmeasured.
