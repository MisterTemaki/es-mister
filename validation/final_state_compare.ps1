param([ValidateSet('idle','controls')][string]$Mode='idle',[string]$Prefix='final-idle-v1')
$ErrorActionPreference='Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
if ($Prefix -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Invalid prefix' }
$root='/media/fat/emulationstation/codex-performance-20261113'
function Remote([string]$Code) {
    $r=$Code | ssh -T -i 'C:/Users/USER/.ssh/id_ed25519_mister' -o BatchMode=yes -o ConnectTimeout=8 root@192.168.15.14 'python3 -'
    if ($LASTEXITCODE -ne 0) { throw 'State test remote check failed' }
    ($r -join "`n")
}
function Bench([string[]]$Arguments) {
    & wsl -d Ubuntu-26.04 -- python3 ./validation/target_run.py @Arguments
    if ($LASTEXITCODE -ne 0) { throw 'State test benchmark failed' }
}
$order=if($Mode -eq 'idle'){@('reference','candidate','candidate','reference')}else{@('reference','candidate')}
'wait 300000','quit' | Set-Content -LiteralPath ./validation/idle-safety.txt -Encoding utf8NoBOM
$script=if($Mode -eq 'idle'){'./validation/idle-safety.txt'}else{'./validation/control-script.txt'}
$index=0
try {
    foreach ($variant in $order) {
        $index++;$label="$Prefix-$index-$variant"
        $enabled=if($variant -eq 'candidate'){'1'}else{'0'}
        $systems=if($variant -eq 'candidate'){'2'}else{'4'}
        $textures=if($variant -eq 'candidate'){'1'}else{'2'}
        Remote "from pathlib import Path;p=Path('$root/$label');p.mkdir(exist_ok=True);assert not (p/'pid').exists(),'Use fresh labels'"
        if ($Mode -eq 'idle') {
            Remote @"
import pathlib,subprocess
p=pathlib.Path('$root/$label');out=(p/'keyboard.log').open('w')
subprocess.Popen(['python3','$root/bin/first_keyboard',str(p),'--trials','8','--idle','15'],stdin=subprocess.DEVNULL,stdout=out,stderr=subprocess.STDOUT,start_new_session=True)
"@
            $ready=$false
            for ($n=0;$n -lt 30;$n++) {
                if ((Remote "from pathlib import Path;print(Path('$root/$label/keyboard-ready').exists())").Trim() -eq 'True') { $ready=$true;break }
                Start-Sleep -Milliseconds 100
            }
            if (-not $ready) { throw 'Persistent keyboard did not become ready before SDL' }
        }
        Bench @('run',$label,'--binary-label',"stage3-final-$variant",'--upload-neon',$enabled,'--opaque-fade',$enabled,'--copy-pld-pair',$enabled,'--system-threads',$systems,'--texture-threads',$textures,'--input-probe','--script',$script,'--trace-redraws-only')
        Remote @"
import pathlib,subprocess
p=pathlib.Path('$root/$label');out=(p/'probe.log').open('w')
subprocess.Popen(['python3','$root/bin/process_probe',(p/'pid').read_text().strip(),str(p/'process.json')],stdin=subprocess.DEVNULL,stdout=out,stderr=subprocess.STDOUT,start_new_session=True)
"@
        $timer=[Diagnostics.Stopwatch]::StartNew();$finished=$false
        while ($timer.Elapsed.TotalSeconds -lt 360) {
            try { $state=Remote "from pathlib import Path;p=Path('$root/$label');assert not (p/'framebuffer-collision.json').exists();print(not Path('/proc/'+(p/'pid').read_text().strip()).exists() and (p/'memory.json').exists() and (p/'process.json').exists())" }
            catch { Write-Output "Read-only status query failed for $label; no relaunch attempted";Start-Sleep -Seconds 2;continue }
            if ($state.Trim() -eq 'True') { $finished=$true;break }
            Start-Sleep -Seconds 5
        }
        if (-not $finished) { throw 'State test timeout' }
        Bench @('collect',$label)
        if ($Mode -eq 'idle') {
            $injections=Get-Content -Raw -LiteralPath "./validation/target-results/$label/injections.json" | ConvertFrom-Json
            $answers=@(Import-Csv -LiteralPath "./validation/target-results/$label/trace.csv.input.csv")
            if ($injections.Count -ne 8 -or $answers.Count -ne 8) { throw 'Idle actions incomplete' }
        }
        Write-Output "COMPLETE $label"
    }
} finally { Bench @('restore') }
