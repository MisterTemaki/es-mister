param([string]$Prefix='cache-screen-v1',[int]$Steps=64,[string]$Order='40,48,48,40',[int]$Detail=0)
$ErrorActionPreference='Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
if ($Prefix -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Invalid prefix' }
$choices=@($Order.Split(','))
if (@($choices | Where-Object { $_ -notin @('40','48','56','64') }).Count) { throw 'Unexpected cache budget' }
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
$index=0
try {
    foreach ($budget in $choices) {
        $index++;$label="$Prefix-$index-m$budget"
        Remote "from pathlib import Path;assert not Path('$root/$label/pid').exists(),'Use a fresh label'"
        Bench @('run',$label,'--binary-label','stage3-opaque-only','--scaled-mb',$budget,'--steps',"$Steps",'--detail',"$Detail",'--trace-redraws-only')
        $timer=[Diagnostics.Stopwatch]::StartNew();$finished=$false
        while ($timer.Elapsed.TotalSeconds -lt 90+$Steps) {
            try {
                $state=Remote "from pathlib import Path;p=Path('$root/$label');print(not Path('/proc/'+(p/'pid').read_text().strip()).exists() and (p/'memory.json').exists())"
                if ($state.Trim() -eq 'True') { $finished=$true;break }
            } catch { Write-Output "Status query failed; benchmark continues: $label" }
            Start-Sleep -Seconds 5
        }
        if (-not $finished) { throw "Cache test timeout: $label" }
        Bench @('collect',$label)
        Write-Output "COMPLETE $label"
    }
} finally { Bench @('restore') }
