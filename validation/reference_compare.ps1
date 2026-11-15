param([string]$Prefix='strict-screen-v1',[int]$Steps=64,[string]$Order='0,1,1,0')
$ErrorActionPreference='Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
if ($Prefix -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Invalid prefix' }
$choices=@($Order.Split(','))
if (@($choices | Where-Object { $_ -notin @('0','1') }).Count) { throw 'Expected 0/1 order' }
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
    foreach ($choice in $choices) {
        $index++;$label="$Prefix-$index-o$choice"
        $binary=if ($choice -eq '0') { 'stage3-transition-reference' } else { 'stage3-transition-candidate' }
        Remote "from pathlib import Path;assert not Path('$root/$label/pid').exists(),'Use a fresh label'"
        Bench @('run',$label,'--binary-label',$binary,'--opaque-fade',$choice,'--steps',"$Steps",'--detail','0')
        $timer=[Diagnostics.Stopwatch]::StartNew();$finished=$false
        while ($timer.Elapsed.TotalSeconds -lt 90+$Steps) {
            try {
                $state=Remote "from pathlib import Path;p=Path('$root/$label');print(not Path('/proc/'+(p/'pid').read_text().strip()).exists() and (p/'memory.json').exists())"
                if ($state.Trim() -eq 'True') { $finished=$true;break }
            } catch { Write-Output "Status query failed; benchmark continues: $label" }
            Start-Sleep -Seconds 5
        }
        if (-not $finished) { throw "Reference test timeout: $label" }
        Bench @('collect',$label)
        Write-Output "COMPLETE $label"
    }
} finally { Bench @('restore') }
