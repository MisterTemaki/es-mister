# Reproducing the tests and how the files are organised

The scripts in this folder are those used during the audit of 13–15/11/2026.
They keep the bench's names, historical variants and paths. The release contains
the raw logs, captures and results, including invalidated runs that are
identified in the report. The JSON summaries are also in
`audit/performance-1080p/final-delivery/`.

## Preparing the directory

The historical scripts expect `es-mister/` and `validation/` as sibling folders.
In PowerShell, starting from an empty folder:

```powershell
git clone --recurse-submodules https://github.com/MisterTemaki/es-mister.git es-mister-audit/es-mister
Copy-Item -LiteralPath './es-mister-audit/es-mister/validation' -Destination './es-mister-audit/validation' -Recurse
Set-Location './es-mister-audit'
```

Run the commands of the [complete guide](../audit/performance-1080p/REPRODUCE.md)
from that `es-mister-audit` folder. The official build `mister/build.sh` can
be run directly at the root of the repository and does not need that copy.

The original bench uses WSL Ubuntu-26.04, a cache in `/mnt/es-mister-codex-arm`,
Windows' OpenSSH, the MiSTer at `192.168.15.14` and the local path of the key
`id_ed25519_mister`. Adjust addresses, paths, cache and expected hashes before
using another machine or another build. The private key is not distributed.
`cross_cache.sh` assumes the cache is prepared; a new machine must first
prepare the dependencies, as in the build guide. No cache is included here.

Use only one test controller at a time, with the MiSTer in MENU and no
other instance of the frontend. The cold-opening tests restart the device;
the core tests launch only the original diagnostic ROM included.
Read the script you choose before running tests that install files or
control the device. The packaging/report scripts reproduce the session's layout
and need the corresponding artefacts, which are not in Git.

## Main entry points

| Goal | Script |
|---|---|
| Navigation of 1,000 commands per round | `final_navigation.ps1` |
| First command after rest, and controls | `final_state_compare.ps1` |
| Warm/cold opening | `final_startup.ps1` |
| Equivalent transitions | `final_animation_compare.ps1` |
| List with cold covers, in and out | `cover_compare.ps1` |
| The app's pixel comparison | `upload_quality.ps1`, `png_compare.py` |
| Generating original covers | `create_cover_fixture.py` |
| Original diagnostic core | `create_nes_probe.py`, `target_lifecycle.py` |
| Payload check without SSH | `check_target_payload.py`, `check_lifecycle_payload.py` |
| Installer and rollback in a temporary directory on the host | `check_final_installer.py` |

`tests/audit/` contains the C++ tests and independent snapshots of the reference.
`arm/*.source.json` are source manifests, not binaries. The accepted ARM ELF
is in the release's update ZIP and is identified by the SHA-256 in the README.
The generator of the ROM `codex-video-probe.nes` and its manifest are included;
the test program has no save RAM and is not a commercial game.

To measure speed, keep double pixel checking, PNG, slow dumps,
signal profiling and forced redraw off. The quality pass is
separate. FPS=60 does not mean 60 full redraws a second.
