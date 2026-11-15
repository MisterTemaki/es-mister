$ErrorActionPreference='Stop'
# Reuse today's completed A2 as A1; three new runs finish a continuous ABBA.
# Preserve its original label, files and timestamps without copying/relabeling data.
& "$PSScriptRoot/final_navigation.ps1" -Prefix final-navigation-current-v1 -Resume -RunLabels @(
    'final-navigation-v1-4-reference',
    'final-navigation-current-v1-2-candidate',
    'final-navigation-current-v1-3-candidate',
    'final-navigation-current-v1-4-reference')
if ($LASTEXITCODE -ne 0) { throw 'Continuous-session navigation comparison failed' }
& wsl -d Ubuntu-26.04 -- python3 ./validation/transition_analyze.py ./validation/target-results/final-navigation-v1-4-reference ./validation/target-results/final-navigation-current-v1-2-candidate ./validation/target-results/final-navigation-current-v1-3-candidate ./validation/target-results/final-navigation-current-v1-4-reference --output ./validation/final-transitions-summary.json
if ($LASTEXITCODE -ne 0) { throw 'Transition analysis failed' }
