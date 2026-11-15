param([string]$Prefix='covers-v1',[int]$Cycles=3,[int]$Span=160,[string]$Order='2,1,1,2',[switch]$Minimal,
      [string]$BinaryLabel='stage3-last-hypotheses',[ValidateSet('current','detailed')][string]$View='current',
      [switch]$FramesOnly,[switch]$ImplementationPair,[int]$StepWait=40,
      [ValidateSet('1','2')][string]$CandidateTextureThreads='1',
      [ValidateSet('0','1')][string]$CopyPldPair='0')
$ErrorActionPreference='Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
if ($Prefix -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Invalid prefix' }
if ($Cycles -lt 1 -or $Cycles -gt 5 -or $Span -lt 1 -or $Span -gt 160) { throw 'Invalid fixture workload' }
if ($StepWait -lt 40 -or $StepWait -gt 2000) { throw 'Invalid per-step wait' }
$choices=@($Order.Split(','))
if ($ImplementationPair) {
    if ($Order -ne '2,2,2,2') { throw 'Implementation pair requires Order 2,2,2,2' }
    $FramesOnly=$true
}
if (@($choices | Where-Object { $_ -notin @('1','2') }).Count) { throw 'Expected 1/2 thread order' }
$root='/media/fat/emulationstation/codex-performance-20261113'
function Remote([string]$Code) {
    $r=$Code | ssh -T -i 'C:/Users/USER/.ssh/id_ed25519_mister' -o BatchMode=yes -o ConnectTimeout=8 root@192.168.15.14 'python3 -'
    if ($LASTEXITCODE -ne 0) { throw 'Remote Python failed' }
    return ($r -join "`n")
}
function Bench([string[]]$Arguments) {
    & wsl -d Ubuntu-26.04 -- python3 ./validation/target_run.py @Arguments
    if ($LASTEXITCODE -ne 0) { throw ('Benchmark failed: '+($Arguments -join ' ')) }
}
$lines=[Collections.Generic.List[string]]::new()
$lines.Add('wait 15000');$lines.Add('stats')
for ($cycle=0;$cycle -lt $Cycles;$cycle++) {
    $lines.Add("log fixture-cycle-$cycle")
    foreach ($direction in @('Down','Up')) {
        for ($n=0;$n -lt $Span;$n++) { $lines.Add("key $direction");$lines.Add("wait $StepWait") }
    }
    foreach ($line in @('key Escape','settle 10000','key Space','wait 1000','key Escape','wait 1000','key Return','settle 10000','stats')) { $lines.Add($line) }
}
$lines.Add('wait 20000');$lines.Add('stats');$lines.Add('quit')
$scriptPath="./validation/$Prefix-script.txt"
$lines | Set-Content -LiteralPath $scriptPath -Encoding utf8NoBOM
$index=0
try {
    foreach ($choice in $choices) {
        $index++;$label="$Prefix-$index-t$choice"
        $binary=$BinaryLabel;$enabled='1';$copy=$CopyPldPair;$decoders=$choice
        if ($ImplementationPair) {
            $variant=if($index -in @(1,4)){'reference'}else{'candidate'}
            $binary="stage3-final-$variant"
            $enabled=if($variant -eq 'candidate'){'1'}else{'0'};$copy=$enabled
            $decoders=if($variant -eq 'candidate'){$CandidateTextureThreads}else{'2'}
            $label="$Prefix-$index-$variant"
        }
        Remote "from pathlib import Path;assert not Path('$root/$label/pid').exists(),'Use a fresh label'"
        $options=@('run',$label,'--binary-label',$binary,'--texture-threads',$decoders,'--cover-fixture','--script',$scriptPath,'--trace-redraws-only','--upload-neon',$enabled,'--opaque-fade',$enabled,'--copy-pld-pair',$copy)
        if ($ImplementationPair) { $options+=@('--system-threads',$(if($enabled -eq '1'){'2'}else{'4'})) }
        if ($View -eq 'detailed') { $options+=@('--fixture-view','detailed') }
        if ($Minimal) { $options+='--trace-off' } elseif (-not $FramesOnly) { $options+='--texture-trace' }
        Bench $options
        Remote @"
import pathlib,subprocess
p=pathlib.Path('$root/$label');out=(p/'probe.log').open('w')
subprocess.Popen(['python3','$root/bin/process_probe',(p/'pid').read_text().strip(),str(p/'process.json')],stdin=subprocess.DEVNULL,stdout=out,stderr=subprocess.STDOUT,start_new_session=True)
"@
        $timer=[Diagnostics.Stopwatch]::StartNew();$finished=$false
        while ($timer.Elapsed.TotalSeconds -lt 1200) {
            try {
                $state=Remote "from pathlib import Path;p=Path('$root/$label');print(not Path('/proc/'+(p/'pid').read_text().strip()).exists() and (p/'memory.json').exists() and (p/'process.json').exists())"
                if ($state.Trim() -eq 'True') { $finished=$true;break }
            } catch { Write-Output "Status query failed; benchmark continues: $label" }
            Start-Sleep -Seconds 10
        }
        if (-not $finished) { throw "Cover test timeout: $label" }
        Bench @('collect',$label)
        $resultPath="./validation/target-results/$label"
        if (Test-Path -LiteralPath "$resultPath/framebuffer-collision.json") { throw 'Overlapping framebuffer owners; run invalid' }
        $memory=Get-Content -Raw -LiteralPath "$resultPath/memory.json" | ConvertFrom-Json
        if ((($memory.framebuffer_owners | Measure-Object -Maximum).Maximum) -ne 1) { throw 'Owner guard missing or invalid' }
        if (-not $Minimal -and -not $FramesOnly -and -not (Test-Path -LiteralPath "$resultPath/textures.csv")) { throw 'Texture telemetry missing; possible abnormal exit' }
        Write-Output "COMPLETE $label"
    }
} finally {
    Bench @('restore')
}
