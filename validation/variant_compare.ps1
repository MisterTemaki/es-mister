param([string]$Prefix='copy-app-v1',[int]$Steps=64,[string]$Order='0,1,1,0',
      [ValidateSet('copy-pld-pair','alpha-scan')][string]$Knob='copy-pld-pair')
$ErrorActionPreference='Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
if ($Prefix -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Invalid prefix' }
$choices=@($Order.Split(','));$root='/media/fat/emulationstation/codex-performance-20261113'
if (@($choices | Where-Object { $_ -notin @('0','1') }).Count) { throw 'Expected 0/1 order' }
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
        $index++;$label="$Prefix-$index-v$choice"
        Remote "from pathlib import Path;assert not Path('$root/$label/pid').exists(),'Use a fresh label'"
        $copy=if($Knob -eq 'copy-pld-pair'){$choice}else{'0'}
        $alpha=if($Knob -eq 'alpha-scan'){$choice}else{'0'}
        Bench @('run',$label,'--binary-label','stage3-last-hypotheses','--upload-neon','1','--opaque-fade','1','--copy-pld-pair',$copy,'--alpha-scan',$alpha,'--steps',"$Steps",'--trace-redraws-only')
        $timer=[Diagnostics.Stopwatch]::StartNew();$finished=$false
        while ($timer.Elapsed.TotalSeconds -lt 90+$Steps) {
            try {
                $state=Remote "from pathlib import Path;p=Path('$root/$label');print(not Path('/proc/'+(p/'pid').read_text().strip()).exists() and (p/'memory.json').exists())"
                if ($state.Trim() -eq 'True') { $finished=$true;break }
            } catch { Write-Output "Status query failed; benchmark continues: $label" }
            Start-Sleep -Seconds 5
        }
        if (-not $finished) { throw "Application test timeout: $label" }
        Bench @('collect',$label);Write-Output "COMPLETE $label"
    }
} finally { Bench @('restore') }
