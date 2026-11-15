param([switch]$PilotOnly,[string]$BinaryLabel='stage3-phases2',[string]$Prefix='startup-workers',
      [string]$WorkerOrder='', [switch]$InputProbe, [ValidateSet('0','1')][string]$SampleNeon='0',[int]$Detail=0,[string]$SampleOrder='', [switch]$TextureTrace,
      [string]$UploadOrder='', [string]$AlphaOrder='', [ValidateSet('0','1')][string]$UploadNeon='0',
      [ValidateSet('0','1')][string]$OpaqueFade='0',[ValidateSet('0','1')][string]$CopyPldPair='0',
      [switch]$ColdStart,[string]$ExpectedBinarySha='',[switch]$Wrapper,
      [ValidateSet('1','2')][string]$TextureThreads='2')
$ErrorActionPreference = 'Stop'
if ($ColdStart -and $ExpectedBinarySha -notmatch '^[0-9a-f]{64}$') { throw 'ColdStart requires the known artifact SHA-256' }
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
$misterKey = 'C:/Users/USER/.ssh/id_ed25519_mister'
$remoteRoot = '/media/fat/emulationstation/codex-performance-20261113'
if ($Wrapper) {
    & scp -i $misterKey -o BatchMode=yes './es-mister/mister/scripts/es-mister.sh' "root@192.168.15.14:$remoteRoot/bin/es-mister-traced-wrapper"
    if ($LASTEXITCODE -ne 0) { throw 'Wrapper upload failed' }
}
& scp -i $misterKey -o BatchMode=yes './es-mister/tests/audit/idle_keyboard.py' "root@192.168.15.14:$remoteRoot/bin/first_keyboard"
if ($LASTEXITCODE -ne 0) { throw 'Keyboard probe upload failed' }
$keyboardOptions=if($Wrapper){"['--startup','--startup-log','es-startup.log']"}else{"['--startup']"}
function Invoke-RemotePython([string]$Code) {
    $result = $Code | & ssh -T -i $misterKey -o BatchMode=yes -o ConnectTimeout=8 root@192.168.15.14 'python3 -'
    if ($LASTEXITCODE -ne 0) { throw 'Remote Python failed' }
    return ($result -join "`n")
}
function Invoke-Bench([string[]]$BenchArgs) {
    & wsl -d Ubuntu-26.04 -- python3 ./validation/target_run.py @BenchArgs
    if ($LASTEXITCODE -ne 0) { throw ('Benchmark failed: ' + ($BenchArgs -join ' ')) }
}
function Wait-Run([string]$Label) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt 120) {
        try {
            $state = Invoke-RemotePython "from pathlib import Path;p=Path('$remoteRoot/$Label');print(not Path('/proc/'+(p/'pid').read_text().strip()).exists() and (p/'memory.json').exists())"
            if ($state.Trim() -eq 'True') { return }
        } catch { Write-Output "Status query failed; benchmark continues independently: $Label" }
        Start-Sleep -Milliseconds 500
    }
    throw "Startup test timeout: $Label"
}
$safetyWait=if($ColdStart){180000}else{60000}
"wait $safetyWait", 'quit' | Set-Content -LiteralPath ./validation/startup-safety.txt -Encoding utf8NoBOM
if ($Prefix -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Invalid prefix' }
$order = if ($WorkerOrder) { @($WorkerOrder.Split(',') | ForEach-Object { [int]$_ }) } elseif ($PilotOnly) { @(4) } else { @(4,2,1,1,2,4,4,2,1,1,2,4,4,2,1,1,2,4) }
foreach ($workers in $order) { if ($workers -notin @(1,2,4)) { throw 'Expected 1, 2 or 4 workers' } }
$sampleSequence = if ($SampleOrder) { @($SampleOrder.Split(',')) } else { @() }
if ($sampleSequence.Count -and ($sampleSequence.Count -ne $order.Count -or @($sampleSequence | Where-Object { $_ -notin @('0','1') }).Count)) { throw 'Sampling order must contain 0/1 for every run' }
$uploadSequence = if ($UploadOrder) { @($UploadOrder.Split(',')) } else { @() }
if ($uploadSequence.Count -and ($uploadSequence.Count -ne $order.Count -or @($uploadSequence | Where-Object { $_ -notin @('0','1') }).Count)) { throw 'Upload order must contain 0/1 for every run' }
$alphaSequence = if ($AlphaOrder) { @($AlphaOrder.Split(',')) } else { @() }
if ($alphaSequence.Count -and ($alphaSequence.Count -ne $order.Count -or @($alphaSequence | Where-Object { $_ -notin @('0','1') }).Count)) { throw 'Alpha order must contain 0/1 for every run' }
$index = 0
try {
    foreach ($workers in $order) {
        $index++
        $runPrefix = if ($PilotOnly -and -not $PSBoundParameters.ContainsKey('Prefix')) { 'startup-pilot' } else { $Prefix }
        $label = "$runPrefix-$index-t$workers"
        $sampleChoice = if ($sampleSequence.Count) { $sampleSequence[$index-1] } else { $SampleNeon }
        if ($sampleSequence.Count) { $label += "-n$sampleChoice" }
        $uploadChoice = if ($uploadSequence.Count) { $uploadSequence[$index-1] } else { $UploadNeon }
        if ($uploadSequence.Count) { $label += "-u$uploadChoice" }
        $alphaChoice = if ($alphaSequence.Count) { $alphaSequence[$index-1] } else { '0' }
        if ($alphaSequence.Count) { $label += "-a$alphaChoice" }
        Write-Output "START $label"
        Invoke-RemotePython @"
import pathlib,subprocess
p=pathlib.Path('$remoteRoot/$label');p.mkdir(exist_ok=True)
assert not (p/'pid').exists(),'Use a fresh label'
out=(p/'keyboard.log').open('w')
proc=subprocess.Popen(['python3','$remoteRoot/bin/first_keyboard',str(p)]+$keyboardOptions,
 stdin=subprocess.DEVNULL,stdout=out,stderr=subprocess.STDOUT,start_new_session=True)
(p/'keyboard-pid').write_text(str(proc.pid))
"@
        $ready = $false
        for ($attempt=0; $attempt -lt 20; $attempt++) {
            if ((Invoke-RemotePython "from pathlib import Path;print(Path('$remoteRoot/$label/keyboard-ready').exists())").Trim() -eq 'True') { $ready=$true; break }
            Start-Sleep -Milliseconds 100
        }
        if (-not $ready) { throw 'Keyboard must be ready before SDL initialization' }
        $options = @('run',$label,'--binary-label',$BinaryLabel,'--system-threads',"$workers",'--script','./validation/startup-safety.txt','--detail',"$Detail",'--sample-neon',$sampleChoice,'--upload-neon',$uploadChoice,'--alpha-scan',$alphaChoice)
        $options+=@('--opaque-fade',$OpaqueFade,'--copy-pld-pair',$CopyPldPair)
        $options+=@('--texture-threads',$TextureThreads)
        if ($ColdStart) { $options+=@('--cold-start','--expected-binary-sha',$ExpectedBinarySha) }
        if ($Wrapper) { $options+='--wrapper' }
        if ($InputProbe) { $options += '--input-probe' }
        if ($TextureTrace) { $options += '--texture-trace' }
        Invoke-Bench $options
        Wait-Run $label
        Invoke-Bench @('collect',$label)
        if ($InputProbe) {
            $startupName=if($Wrapper){'es-startup.log'}else{'startup.log'}
            $text=Get-Content -Raw -LiteralPath "./validation/target-results/$label/$startupName"
            $moves=[regex]::Matches($text,'input selection result: (\d+) -> (\d+)')
            if ($moves.Count -ne 1 -or $moves[0].Groups[1].Value -eq $moves[0].Groups[2].Value) { throw 'The first injected key did not change the selection' }
            $answers=@(Import-Csv -LiteralPath "./validation/target-results/$label/trace.csv.input.csv")
            if ($answers.Count -ne 1) { throw 'Expected exactly one first input response' }
        }
        $configuration=Get-Content -Raw -LiteralPath "./validation/target-results/$label/configuration.json" | ConvertFrom-Json
        if (-not $configuration.binary_sha_verified_after_run) { throw 'Post-run binary verification failed' }
        Write-Output "COMPLETE $label"
    }
} finally {
    Invoke-Bench @('restore')
}
