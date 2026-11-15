$ErrorActionPreference='Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
& pwsh -NoProfile -File ./validation/startup_compare.ps1 -BinaryLabel stage3-last-hypotheses -Prefix startup-alpha-v1 -WorkerOrder 4,4,4,4,4,4,4,4,4,4,4,4 -AlphaOrder 0,1,1,0,0,1,1,0,0,1,1,0 -UploadNeon 1 -InputProbe
if ($LASTEXITCODE -ne 0) { throw 'Alpha scan startup comparison failed' }
& scp -i 'C:/Users/USER/.ssh/id_ed25519_mister' -o BatchMode=yes './es-mister/tests/audit/process_probe.py' 'root@192.168.15.14:/media/fat/emulationstation/codex-performance-20261113/bin/process_probe'
if ($LASTEXITCODE -ne 0) { throw 'Process monitor transfer failed' }
& pwsh -NoProfile -File ./validation/cover_compare.ps1 -Prefix covers-final-v1
if ($LASTEXITCODE -ne 0) { throw 'Cover loader comparison failed' }
& pwsh -NoProfile -File ./validation/cover_compare.ps1 -Prefix covers-minimal-v1 -Cycles 2 -Order 2 -Minimal
if ($LASTEXITCODE -ne 0) { throw 'Minimal telemetry memory pass failed' }
Write-Output 'ALPHA STARTUP AND COVER ACCEPTANCE COMPLETE'
