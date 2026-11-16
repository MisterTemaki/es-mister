# The audit's regression tests

Local tests on Linux, with C++17, CMake, libcurl, SDL2, pugixml and OpenSSL (the command).
They do not access the MiSTer's devices nor run games. HTTP/TLS servers use only loopback;
files are created in temporary directories. Logging and the choice of paths are isolated;
the HTTP, ZIP, string, file and settings implementations are the project's.

```sh
cmake -S tests/audit -B build-audit -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-audit -j2
python3 tests/audit/run.py build-audit/audit_regressions
python3 tests/audit/run_settings.py build-audit/audit_settings
python3 tests/audit/run_renderer.py build-audit/audit_renderer
build-audit/audit_http_server
build-audit/audit_thread_pool
build-audit/audit_copy_file
build-audit/audit_similarity
python3 tests/audit/run_theme.py build-audit/audit_theme
```

AddressSanitizer, LeakSanitizer and UndefinedBehaviorSanitizer are enabled by default.
The cross-filesystem scenario runs when `/dev/shm` exists and is on a different device
from the temporary directory. The report records that it ran on this machine.

The additional suites cover HTTP shutdown during an active handler and an immediate start,
the pool's reuse/cancellation/exceptions, a real copy write failure preserving the
destination, equivalence of the search and metadata of 100,000 bytes, theme installation with
an invalid CRC/traversal/rollback, and the backend's pixels with transparent commands,
additive blending, stencil and recreation. The old similarity reference is only a
test oracle for limited inputs, never code compiled into the application.

`raster_workload.cpp` allows a differential comparison of 16 scenes with the previous
rasteriser on x86 and ARM/NEON. The `bench` mode is synthetic and does not measure TV latency.
The DE10-Nano navigation measurements are in `audit/performance-1080p/RESULTS.md`.

To test the interface rendered in software, without a framebuffer, with an artificial catalogue:

```sh
python3 tests/audit/smoke.py build-native/emulationstation --output-dir build-audit/smoke
```

This test creates a `resources` link next to the binary when it does not exist and saves logs/captures
in the folder given. It validates help, start-up, navigation and shutdown. It does not measure the
FPGA's performance nor test the framebuffer, NEON, physical ALSA or launching cores.
