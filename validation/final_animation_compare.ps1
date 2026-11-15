param([string]$Prefix='final-animations-v1')
$ErrorActionPreference='Stop'
& "$PSScriptRoot/final_navigation.ps1" -Prefix $Prefix -Steps 16 -StepWait 1800 -TraceAllFrames
if ($LASTEXITCODE -ne 0) { throw 'Quiet animation scenario failed' }
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
& wsl -d Ubuntu-26.04 -- python3 ./validation/transition_analyze.py "./validation/target-results/$Prefix-1-reference" "./validation/target-results/$Prefix-2-candidate" "./validation/target-results/$Prefix-3-candidate" "./validation/target-results/$Prefix-4-reference" --require-complete --output "./validation/$Prefix-transitions.json"
if ($LASTEXITCODE -ne 0) { throw 'Animation completion was not observed for every command' }
