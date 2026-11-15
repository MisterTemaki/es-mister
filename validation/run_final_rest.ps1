$ErrorActionPreference='Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
function Step([string]$Script,[string[]]$Options) {
    & pwsh -NoProfile -File "./validation/$Script" @Options
    if ($LASTEXITCODE -ne 0) { throw "Acceptance stopped at $Script" }
}
function Analyze([string[]]$Options) {
    & wsl -d Ubuntu-26.04 -- python3 @Options
    if ($LASTEXITCODE -ne 0) { throw 'Acceptance analysis failed' }
}
if (-not (Test-Path -LiteralPath './validation/final-navigation-current-v1-summary.json')) { throw 'Finish the continuous navigation controller first' }
if (-not (Test-Path -LiteralPath './validation/final-transitions-summary.json')) {
    Analyze @('./validation/transition_analyze.py','./validation/target-results/final-navigation-v1-4-reference','./validation/target-results/final-navigation-current-v1-2-candidate','./validation/target-results/final-navigation-current-v1-3-candidate','./validation/target-results/final-navigation-current-v1-4-reference','--output','./validation/final-transitions-summary.json')
}
Step final_state_compare.ps1 @('-Mode','controls','-Prefix','final-controls-v1')
Analyze @('./validation/state_analyze.py','controls','./validation/target-results/final-controls-v1-1-reference','./validation/target-results/final-controls-v1-2-candidate','--output','./validation/final-controls-v1-summary.json')
Step final_state_compare.ps1 @('-Mode','idle','-Prefix','final-idle-v1')
Analyze @('./validation/state_analyze.py','idle','./validation/target-results/final-idle-v1-1-reference','./validation/target-results/final-idle-v1-2-candidate','./validation/target-results/final-idle-v1-3-candidate','./validation/target-results/final-idle-v1-4-reference','--output','./validation/final-idle-v1-summary.json')
foreach ($mode in @('warm','cold')) {
    $options=@('-Prefix',"startup-full-$mode-v1")
    if ($mode -eq 'cold') { $options+='-Reboot' }
    Step final_startup.ps1 $options
    $runs=if($mode -eq 'warm'){@('1-reference-1-t4','2-candidate-1-t2','3-candidate-1-t2','4-reference-1-t4')}else{@('1-reference-1-t4','2-candidate-1-t2','3-candidate-1-t2','4-reference-1-t4')}
    $analysis=@('./validation/startup_analyze.py')+@($runs | ForEach-Object { "./validation/target-results/startup-full-$mode-v1-$_" })+@('--summary-output',"./validation/startup-full-$mode-v1-summary.json")
    Analyze $analysis
}
Step final_animation_compare.ps1 @('-Prefix','final-animations-v1')
Analyze @('./validation/startup_final_verify.py')
Analyze @('./validation/target_lifecycle.py','core','final-core-v1')
Analyze @('./validation/target_lifecycle.py','core','final-core-speed-v2','--no-check')
Write-Output 'FINAL REMAINING ACCEPTANCE COMPLETE'
