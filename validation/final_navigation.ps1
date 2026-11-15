param([string]$Prefix='final-navigation-v1',[int]$Steps=1000,
      [ValidateSet('1','2')][string]$CandidateTextureThreads='1',
      [ValidatePattern('^[a-f0-9]{64}$')][string]$CandidateSha='872dd340826f39da1bb14371681bb0decc4f4d3c640ae93e646ef46337465b55',
      [switch]$Resume,[string[]]$RunLabels=@(),[ValidateRange(450,5000)][int]$StepWait=450,[switch]$TraceAllFrames)
$ErrorActionPreference='Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
if ($Prefix -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Invalid prefix' }
if ($RunLabels.Count -and ($RunLabels.Count -ne 4 -or @($RunLabels | Where-Object { $_ -notmatch '^[a-zA-Z0-9_-]+$' }).Count)) { throw 'Expected four valid explicit run labels' }
function Bench([string[]]$Arguments) {
    & wsl -d Ubuntu-26.04 -- python3 ./validation/target_run.py @Arguments
    if ($LASTEXITCODE -ne 0) { throw 'Final navigation operation failed' }
}
function Remote([string]$Code) {
    # This function is read-only. Retrying it never starts another frontend.
    for ($attempt=0;$attempt -lt 3;$attempt++) {
        $r=$Code | ssh -T -i 'C:/Users/USER/.ssh/id_ed25519_mister' -o BatchMode=yes -o ConnectTimeout=8 root@192.168.15.14 'python3 -'
        if ($LASTEXITCODE -eq 0) { return ($r -join "`n") }
        Start-Sleep -Seconds 2
    }
    throw 'Remote status unavailable; no relaunch attempted'
}
$root='/media/fat/emulationstation/codex-performance-20261113'
if ($StepWait -ne 450) {
    $lines=@('wait 14000','log navigation-start','stats')
    for ($step=0;$step -lt $Steps;$step++) {
        $key=if($step%8 -lt 4){'Right'}else{'Left'}
        $lines+=@("key $key","wait $StepWait")
    }
    $lines+=@('wait 1500','log navigation-end','stats','quit')
    $lines | Set-Content -LiteralPath "./validation/$Prefix-script.txt" -Encoding utf8NoBOM
}
$index=0;$labels=@()
try {
    foreach ($variant in @('reference','candidate','candidate','reference')) {
        $index++;$label=if($RunLabels.Count){$RunLabels[$index-1]}else{"$Prefix-$index-$variant"};$labels+=$label
        $exists=(Remote "from pathlib import Path;print(Path('$root/$label/pid').exists())").Trim() -eq 'True'
        if ($exists -and -not $Resume) { throw 'Use fresh labels or explicitly resume this comparison' }
        $enabled=if($variant -eq 'candidate'){'1'}else{'0'}
        $workers=if($variant -eq 'candidate'){'2'}else{'4'}
        $decoders=if($variant -eq 'candidate'){$CandidateTextureThreads}else{'2'}
        Write-Output "START $label"
        if (-not $exists) {
            $options=@('run',$label,'--binary-label',"stage3-final-$variant",'--upload-neon',$enabled,'--opaque-fade',$enabled,'--copy-pld-pair',$enabled,'--alpha-scan','0','--system-threads',$workers,'--texture-threads',$decoders,'--steps',"$Steps")
            if (-not $TraceAllFrames) { $options+='--trace-redraws-only' }
            if ($StepWait -ne 450) { $options+=@('--script',"./validation/$Prefix-script.txt") }
            Bench $options
        } else { Write-Output "RESUME existing run $label; no process started" }
        $timer=[Diagnostics.Stopwatch]::StartNew();$finished=$false
        while ($timer.Elapsed.TotalSeconds -lt 240+$Steps*1.2) {
            try {
                $status=Remote "from pathlib import Path;p=Path('$root/$label');assert not (p/'framebuffer-collision.json').exists(),'Overlapping owners';print(not Path('/proc/'+(p/'pid').read_text().strip()).exists() and (p/'memory.json').exists())"
                if ($status.Trim() -eq 'True') { $finished=$true;break }
            } catch { Write-Output "Status unavailable for $label; autonomous run continues, no relaunch attempted";Start-Sleep -Seconds 3;continue }
            Start-Sleep -Seconds 10
        }
        if (-not $finished) { throw 'Final navigation timeout' }
        Bench @('collect',$label)
        $resultPath="./validation/target-results/$label"
        $answers=@(Import-Csv -LiteralPath "$resultPath/trace.csv.input.csv")
        if ($answers.Count -ne $Steps) { throw 'Incomplete input responses; do not advance to another run' }
        $memory=Get-Content -Raw -LiteralPath "$resultPath/memory.json" | ConvertFrom-Json
        if ((($memory.framebuffer_owners | Measure-Object -Maximum).Maximum) -ne 1) { throw 'Owner guard missing or invalid' }
        Write-Output "COMPLETE $label"
    }
    $runs=@($labels | ForEach-Object { './validation/target-results/'+$_ })
    & wsl -d Ubuntu-26.04 -- python3 ./validation/combined_compare.py @runs --reference-sha 1c29852e682db65a01590674986e00c937ee14a63dc44d053700ff4a325393a6 --candidate-sha $CandidateSha --expected $Steps --alpha 0 --copy 1 --texture $CandidateTextureThreads --output "./validation/$Prefix-summary.json"
    if ($LASTEXITCODE -ne 0) { throw 'Final acceptance analysis failed' }
} finally { Bench @('restore') }
