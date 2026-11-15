$ErrorActionPreference='Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
& pwsh -NoProfile -File ./validation/cover_compare.ps1 -Prefix covers-minimal-final-v1 -Cycles 2 -Span 80 -Order 1 -Minimal -BinaryLabel stage3-final-candidate -CopyPldPair 1
if ($LASTEXITCODE -ne 0) { throw 'Minimal memory acceptance failed' }
& pwsh -NoProfile -File ./validation/cover_compare.ps1 -Prefix covers-source-final-v1 -Cycles 1 -Span 24 -Order 2,2,2,2 -ImplementationPair -FramesOnly -CandidateTextureThreads 1
if ($LASTEXITCODE -ne 0) { throw 'Combined source cover acceptance failed' }
# Exercise the full isolated wrapper and post-exit ELF verification. This is a
# warm smoke of the cold-measurement mechanism; it is not a reboot measurement.
& pwsh -NoProfile -File ./validation/startup_compare.ps1 -Prefix wrapper-pilot-v1 -BinaryLabel stage3-final-candidate -WorkerOrder 2 -TextureThreads 1 -UploadNeon 1 -OpaqueFade 1 -CopyPldPair 1 -InputProbe -Wrapper -ColdStart -ExpectedBinarySha 872dd340826f39da1bb14371681bb0decc4f4d3c640ae93e646ef46337465b55
if ($LASTEXITCODE -ne 0) { throw 'Full wrapper pilot failed; inspect before long navigation' }
& pwsh -NoProfile -File ./validation/final_navigation.ps1
if ($LASTEXITCODE -ne 0) { throw 'Combined long navigation acceptance failed' }
Write-Output 'MINIMAL/COLD COVERS/WRAPPER PILOT/FOUR LONG RUNS COMPLETE'
