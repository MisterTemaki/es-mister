$ErrorActionPreference='Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
& pwsh -NoProfile -File ./validation/upload_quality.ps1 -Prefix alpha-logical-v1 -Script ./validation/quality-logical.txt -BinaryLabel stage3-last-hypotheses -Knob alpha-scan -UploadNeon 1 -OpaqueFade 1
if ($LASTEXITCODE -ne 0) { throw 'Alpha scan quality failed' }
& pwsh -NoProfile -File ./validation/startup_compare.ps1 -BinaryLabel stage3-last-hypotheses -Prefix startup-alpha-v1 -WorkerOrder 4,4,4,4,4,4,4,4,4,4,4,4 -AlphaOrder 0,1,1,0,0,1,1,0,0,1,1,0 -UploadNeon 1 -InputProbe
if ($LASTEXITCODE -ne 0) { throw 'Alpha scan startup failed' }
& pwsh -NoProfile -File ./validation/variant_compare.ps1 -Prefix copy-app-v1 -Knob copy-pld-pair
if ($LASTEXITCODE -ne 0) { throw 'Copy prefetch application test failed' }
Write-Output 'FINAL TWO HYPOTHESES APPLICATION TESTS COMPLETE'
