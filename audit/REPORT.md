# es-mister audit — stage 1

> Historical record of the first stage. The follow-up, with fixes P01–P06,
> the ARM build and real measurements, is in [performance-1080p/RESULTS.md](performance-1080p/RESULTS.md).
> The limits below describe the state of that delivery and do not replace the update.

Date: 13/11/2026. State: this stage's fixes implemented and validated locally; full audit pending.

**Material and comparison baseline**

The ZIP provided holds 1,008 files, 46,509,260 bytes uncompressed and 434 text files in `es-app/src` and `es-core/src`, with 168,365 lines. The Git history and the contents of the `external/pugixml` submodule are not present. `5b5c7bc` is the identifier from the name received; it could not be confirmed against the original history.

SHA-256 of the original ZIP: `f87ee31502c598e3e6e5c526df0a2e770a8d7bf79ca9fb967ad329826669863a`.

The original was preserved. The analysis and the changes took place in an isolated copy, with a local reference commit. No other project in the folder was changed. Documents and comments were treated as evidence about the project, without adopting instructions that strayed from the request.

**Architecture identified**

| Component | Responsibility, state and boundaries |
|---|---|
| `es-app/src/main.cpp` | Arguments, asynchronous start-up, window, catalogue, input, services and main loop. |
| `SystemData`, `FileData`, collections and filters | Systems configured by XML, game scanning and catalogue organisation. `FileData::launchGame` frees resources and runs the configured command. |
| `Gamelist`, `MetaData`, `Settings`, `SystemConf`, save states | XML/configuration persistence and metadata recovery. These flows handle failures to different degrees. |
| `es-core` | Graphical components, themes, fonts, textures, input, audio, resources, utilities and network. |
| Renderers | OpenGL/GLES and the software backend. The software backend records commands, draws bands with workers and copies the changed regions to an in-memory output or the Linux framebuffer. |
| HTTP, scraping and installation | `HttpReq` shares a libcurl multi handle protected by a mutex; callbacks receive input from the network. `ZipFile` uses miniz and adapts CP437/UTF-8 names. |
| Local HTTP service | The application's query and mutation endpoints. `PublicWebAccess` starts disabled; enabling it widens the trust boundary. Life cycle and endpoints still need a detailed review. |
| MiSTer | Scripts generate `es_systems.cfg`, ask for the console, start ES and switch cores through MGL and `/dev/MiSTer_cmd`. The helper uses the framebuffer, consoles and uinput. `systems.tsv` defines the systems. |
| Build and distribution | CMake/C++17. `mister/build.sh` uses Zig/musl for ARMv7 Cortex-A9; `package.sh` gathers binary, helper, theme, controllers and translations. The port uses stb, software rendering and VLC stubs; it does not provide HTTPS in the current ARM build. |

Invariants preserved: existing XML/configuration formats; commands and the systems table; old files kept during download and save failures; CP437/UTF-8 naming rules; explicit choice of destination in the ZIP viewer; platform-conditional support. The TLS change now rejects untrusted certificates in builds with HTTPS. MiSTer protocols, emulators and the MGL format were not changed.

**Effective coverage**

`coverage.csv` and `coverage.json` record each original file, size, hash before/after, status and depth. 16 files are analysed, 228 partially analysed and 764 not analysed. The new test and documentation files are listed by the diff, outside the count of the original.

"Analysed" at this stage means a manual review of the file; it does not mean proof that there are no bugs. The detailed review covers `HttpReq`, `ZipFile` and the 12 files in `mister`. Settings persistence, the file utility, installation, input, start-up and concurrency had the flows indicated reviewed, without claiming a full review of those files.

The two Cppcheck runs reached 208 and 204 units, respectively, of the build with MiSTer conditionals. The automatic analysis of a unit does not necessarily cover every configuration or all of its headers. Some warnings reveal limits of the tool's configuration, including the expansion of `CURL_AT_LEAST_VERSION` in `HttpReq`; that file was built and tested separately with sanitizers.

525 files were classed as third-party code, resources or catalogues left out of this stage's manual review: `external`, assets, translations, `httplib.h`, miniz in `zip_file.hpp` and the `han.h` table. Their interfaces with files, the network and decoding remain relevant. No CVE audit, full review of the media parsers, fuzzing or validation of all distribution licences was carried out. The exclusions are not a statement that those dependencies are safe.

**Plan carried out and acceptance**

| Priority | Goal and components | Acceptance and validation | Risk considered |
|---|---|---|---|
| 1 | Memory and trust: HTTP callbacks, TLS and ZIP | Buffers with explicit sizes, names converted and freed correctly, ZIP with no escape through paths, untrusted certificate rejected. Tests with ASan/UBSan/LSan. | Malformed input; name compatibility; TLS on other platforms. |
| 2 | Integrity: downloads, renaming and Settings | Previous file preserved on HTTP error, interruption, flush failure and rename failure; Settings keeps the changes for a new attempt and only fires events after success. | Disk failure; Windows/POSIX differences; interrupted replacement. |
| 3 | Build: pugixml and MiSTer translations | Local build of the executable with `USE_SYSTEM_PUGIXML=ON`; MiSTer no longer runs a gettext target incompatible with its `.po` reader. | Missing submodule; tools available on the host; cross compilation not run. |

**Findings fixed**

All the findings below are confirmed by inspecting the code. The evidence column tells dynamic reproduction apart from verification by contract or by compilation. None was classed as critical; those with the greatest impact were classed as high under the conditions given.

| ID / severity | Place, cause and condition | Impact | Fix and evidence |
|---|---|---|---|
| A01 — high | `HttpReq.cpp:565` original, `header_callback`: treated the libcurl buffer as a NUL-terminated string. | Out-of-bounds read on receiving a header without a terminator. | String built with the length received. ASan reproduced `heap-buffer-overflow` before; the `header` test passes after. The contract confirms the buffer is not NUL-terminated. [libcurl](https://curl.se/libcurl/c/CURLOPT_HEADERFUNCTION.html) |
| A02 — high on LP64 | `HttpReq::status`: passed `int*` to `CURLINFO_RESPONSE_CODE`, which requires `long*`. | Write past the variable on platforms with a 64-bit `long`; on ARM32 the sizes may coincide. | Variable changed to `long`. Checked against the contract, built and run with local requests; no isolated reproduction of this overwrite inside the uninstrumented library is claimed. [Contract](https://curl.se/libcurl/c/CURLINFO_RESPONSE_CODE.html) |
| A03 — high | `HttpReq::performRequest`: `CURLOPT_SSL_VERIFYPEER=0` on every request. | Servers with untrusted certificates could deliver content or receive credentials in builds with TLS. | The disabling was removed. A self-signed certificate was accepted before and rejected after; the test with an explicitly trusted CA validates the success path. It does not add HTTPS to the ARM port. [TLS verification](https://curl.se/libcurl/c/CURLOPT_SSL_VERIFYPEER.html) |
| A04 — high | The download removed the destination when the request was created; `renameFile` removed the destination before trying to move and reported success for a missing source. | Loss of the previous file through a network error or a rename failure. | Destination kept until completion; rename with native replacement, with no partial-copy fallback. Tests of cancellation, 404, incomplete response, replacement and a real EXDEV failure passed. |
| A05 — high | `HttpReq::closeStream` ignored `fflush` and `fclose` failures; the callback did not check for a partial write. | A download could be marked complete after a disk failure on the final buffer. | Stream error propagated; partial write detected. `/dev/full` reproduced the failure before, and it was detected after. |
| A06 — medium | The `customHeaders` list allocated with `curl_slist_append` had no owner. | Leak in repeated requests, including those of authenticated scrapers. | List stored in the request, handled on allocation failure and freed in the destructor. Twenty cycles run under LeakSanitizer. |
| A07 — low | The file-writing callback returned `nmemb` instead of bytes. | Broke the contract when `size != 1`; libcurl's usual path uses size 1. | Returns the total number of bytes; test with `size=2`, `nmemb=3`. |
| A08 — medium | `getContent` read only the temporary file, which had already been renamed on success. | Empty content after a completed download. | Reads the destination after success and keeps the temporary file in the other states; download-and-read test passed. |
| A09 — medium | Headers of intermediate responses stayed in the map. | Redirect metadata could be mistaken for that of the final response. | Map cleared on receiving a new status line; `header-reset` test. |
| A10 — high | `ZipFile.cpp:119` original: `new[]` paired with `delete` in the CP437 conversion. | Undefined behaviour when listing ZIPs without the UTF-8 flag, including ordinary ASCII names. | `delete[]`; ASan reproduced `alloc-dealloc-mismatch`, fixed in the listing and extraction tests. |
| A11 — medium | `ZipFile::load` reused the previous file's name map. | Failure to extract/hash after switching to a ZIP with a different encoding. | State reset on every load, including an open failure; a CP437 test followed by UTF-8 checks a known MD5. |
| A12 — high | `ZipFile::extract` concatenated the ZIP's names; `ApiSystem::unzipFile` also created directories without validation. | A malicious ZIP could write outside the chosen folder or follow an existing link. | Components and links validated before extraction, directories included; parents of nested files created; extraction failures propagated. Tests cover `..`, absolute paths, Windows drives, backslashes and a link pointing outside. At the baseline the ZIP cases aborted first on A10; the independent evidence for traversal is the inspection of the flow. |
| A13 — medium | miniz used unaligned whole-word reads on x86. | UBSan flagged undefined behaviour when reading the headers. | The portable byte-by-byte read path selected, without editing the third-party code. The ZIP tests produce no diagnostics with UBSan set to abort. ARM already used that path. |
| A14 — high | `Settings::saveFile` ignored `save_file`, cleared `mWasChanged` before writing and announced success. | Loss or truncation of settings, and no way to retry without a new edit. | Write to a temporary file, flush/close checked, replacement by rename and the flag cleared after success. Events only after the commit. The test reproduced the false success before; after, it passed through failure, retry, preservation of the previous file and valid XML. |
| A15 — medium | `USE_SYSTEM_PUGIXML=ON` still left 16 includes pointing at the submodule. | The build with the system library failed, especially from the ZIP, which does not contain the submodule. | The `utils/Xml.h` header selects the source, keeping the embedded path when the option is off. The full executable built with the system's pugixml 1.14. The path with the submodule was not built. |
| A16 — medium | MiSTer included the gettext target, whose paths use `dir` as the source root although in this variant it is the build folder. The script then discarded `locale/` with a checkout. | The build depended on the host's tools and could discard local work on translations. | gettext target excluded on MiSTer, which already reads `.po`; the checkout removed. The variant built and the script's syntax checked. The full ARM build was not run. |

**Results observed**

- The 12 initial scenarios failed at the baseline; four ZIP cases were cut short by the same wrong memory release, so they are not four independent reproductions.
- The final HTTP/ZIP/files suite has 17 scenarios, including TLS with a trusted CA. The exact result is in `evidence/regressions-final.log`.
- The Settings suite passed: failure to create/replace, retry, integrity of the previous file, XML and event emission. It is in `evidence/settings-final.log`.
- ASan, LSan and UBSan were used in the isolated suites. The full executable was built in Debug, without global sanitizer instrumentation.
- The local executable is **ELF x86-64**, with MiSTer conditionals. `--help`, start-up, navigation to a test game and shutdown returned code zero. Screenshots and log record the 640×360 in-memory renderer and a clean shutdown. No ROM was run.
- The environment without a sound card produced the expected ALSA messages; the query to an unavailable Zaparoo service produced a connection error. These results were not classed as failures of the fixed software.
- Initial Cppcheck: 930 raw warnings. Cppcheck of the configuration: 1,533 raw warnings, including 33 the tool classes as `error`. They are not 33 confirmed bugs. There are repeated headers, unsupported configurations and candidates that need reproduction. ShellCheck: 17 findings, no syntax errors. The raw files were preserved.
- No performance gain was measured or attributed to the changes.

The local smoke test recorded 18 frames, 16 drawn: average rasterisation of **0.4 ms** and maximum of **1.3 ms**; recorded frame time of **31.2 ms** on average and a peak of **403.7 ms** in the short test, which includes start-up. The single simulated key had a response of **20 ms**. The test was limited to 30 FPS, in Debug, x86-64/WSL, 640×360, in-memory output and an artificial catalogue without the Carbon theme. These figures are not a MiSTer benchmark, do not establish a sustained FPS and do not show an improvement over the original. The percentile value printed by the renderer has no statistical meaning with a single key.

**Traceable pending items for the next stages**

| ID / priority | Evidence and state | Next validation / acceptance |
|---|---|---|
| P01 — high | Confirmed by inspection: `HttpServerThread::~HttpServerThread` destroys the server before the join; the worker may still use that object. The smoke test did not reproduce a failure. | Review ownership and synchronisation, test shutdown during requests and an immediate start/stop under sanitizers. |
| P02 — high | Confirmed by inspection: `ThreadPool` shares bool flags without synchronisation; `wait()` lets workers leave while keeping the running state. | Test reuse, cancellation, a zero `hardware_concurrency`, exceptions and the PDF/CBZ consumers before changing the contract. |
| P03 — high | Confirmed by inspection: `copyFile` ignores write/close errors and returns success for a missing source. | Test a full disk, identical source/destination and the save-state consumers. Not yet fixed at this stage. |
| P04 — high | Confirmed by inspecting the consumers: some installers ignore the return of unzip/rename and replace the previous content before the whole flow completes. | Staged installation, failure handling and recovery; tests with ZIPs with an invalid CRC and interruption. Propagating the error in the ZIP wrapper alone does not make these installers transactional. |
| P05 — medium | Confirmed by inspection: `jw_distance` uses two fixed vectors of 65,000 integers on the stack and indexes them by the strings' lengths. The specific warning about `s2[j]` remains a candidate, not reproduced. | Test long metadata and preserve relevance results before fixing the limits/storage. |
| P06 — medium | Confirmed reproducibility risk: RapidJSON and the controller source still use `master`; pugixml submodule missing. There is no evidence of a compromised dependency. | Recover the exact revisions, record hashes and build ARM reproducibly. Without updating dependencies wholesale. |
| P07 — investigation | Unconfirmed warnings in RetroAchievements, NetPlay, stb, miniz and other areas; manual review of much of the UI, scraping, images and settings still pending. | Reproduce/discard each candidate, review the remaining files and update the coverage. |

**Limits and residual risks of this delivery**

There was no test on the DE10-Nano: framebuffer, uinput, NEON, real audio, hotplug, MGL and the return from the core need validation on the device. Windows and the OpenGL/GLES renderers were not built. The Windows rename code was reviewed, without running on that system.

Renaming keeps the previous file in the operation errors tested; durability through power loss is not claimed, since no fsync protocol was added. Temporary files have fixed names: concurrency on the same destination remains outside the tested contract. The ZIP protection covers malicious names and pre-existing links; it offers no isolation from a local process that swaps directories/links during extraction. No limits on the total uncompressed size were added.

The 764 files still marked "Not analysed" and the pending parts of the 228 partial files prevent declaring the full audit complete. The next stages must deal with items P01–P07 and the rest of the inventory. This delivery closes the HTTP, ZIP, settings and build increment, explicitly preserving the pending items.

**Building and reproducing locally**

On Linux with C++17, CMake, Ninja, SDL2, SDL2_mixer, FreeType, libcurl, ALSA, libVLC (headers), RapidJSON and pugixml installed:

```sh
cmake -S . -B build-native -G Ninja -DMISTER=ON -DCEC=OFF \
  -DUSE_SYSTEM_PUGIXML=ON -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build-native -j2 --target emulationstation
cmake -S tests/audit -B build-audit -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-audit -j2
python3 tests/audit/run.py build-audit/audit_regressions
python3 tests/audit/run_settings.py build-audit/audit_settings
python3 tests/audit/smoke.py build-native/emulationstation --output-dir build-audit/smoke
```

`MISTER=ON` selects features, not the compiler's architecture. **Do not copy the x86-64 validation ELF to the MiSTer.** For ARM, the project's command is still `bash mister/build.sh all`, after preparing Zig and the tools documented in the script and restoring the pugixml submodule at the appropriate revision. The ZIP does not state the submodule's revision; the cross compilation and the SD package were not produced at this stage.

The tests require no data migration. Invalid certificates are now refused; for private services, install the correct CA in the environment with TLS. Existing XML files keep their format.
