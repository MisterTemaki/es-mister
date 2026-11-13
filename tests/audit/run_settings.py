#!/usr/bin/env python3
"""Run settings persistence checks using an isolated home directory."""
import os
import pathlib
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix='es-settings-') as home:
    result = subprocess.run([str(pathlib.Path(sys.argv[1]).resolve())],
                            env=dict(os.environ, ES_TEST_HOME=home,
                                     ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                                     UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'),
                            timeout=20)
raise SystemExit(result.returncode)
