param([string]$Prefix='opaque-screen',[int]$Steps=64,[string]$Order='0,1,1,0',
      [switch]$Quality,[int]$Detail=0)
$ErrorActionPreference='Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
if ($Prefix -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Invalid prefix' }
$choices=@($Order.Split(','))
if (@($choices | Where-Object { $_ -notin @('0','1') }).Count) { throw 'Expected 0/1 order' }
$remoteRoot='/media/fat/emulationstation/codex-performance-20261113'
function Remote([string]$Code) {
    $r=$Code | & ssh -T -i 'C:/Users/USER/.ssh/id_ed25519_mister' -o BatchMode=yes -o ConnectTimeout=8 root@192.168.15.14 'python3 -'
    if ($LASTEXITCODE -ne 0) { throw 'Remote Python failed' }
    return ($r -join "`n")
}
function Bench([string[]]$Arguments) {
    & wsl -d Ubuntu-26.04 -- python3 ./validation/target_run.py @Arguments
    if ($LASTEXITCODE -ne 0) { throw ('Benchmark failed: '+($Arguments -join ' ')) }
}
function Wait-Run([string]$Label) {
    $deadline=[DateTime]::UtcNow.AddSeconds(90+$Steps*0.8)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $state=Remote "from pathlib import Path;p=Path('$remoteRoot/$Label');print(not Path('/proc/'+(p/'pid').read_text().strip()).exists() and (p/'memory.json').exists())"
            if ($state.Trim() -eq 'True') { return }
        } catch { Write-Output "Status query failed; benchmark continues: $Label" }
        Start-Sleep -Seconds 5
    }
    throw "Benchmark timeout: $Label"
}
$index=0
try {
    foreach ($choice in $choices) {
        $index++
        $label="$Prefix-$index-o$choice"
        Remote "from pathlib import Path;assert not Path('$remoteRoot/$label/pid').exists(),'Use a fresh label'"
        Write-Output "START $label"
        $options=@('run',$label,'--binary-label','stage3-opaque-only','--opaque-fade',$choice,'--detail',"$Detail",'--steps',"$Steps",'--trace-redraws-only')
        if ($Quality) { $options+=@('--check','--script','./validation/quality-script.txt') }
        Bench $options
        Wait-Run $label
        Bench @('collect',$label)
        Write-Output "COMPLETE $label"
    }
} finally {
    Bench @('restore')
}
