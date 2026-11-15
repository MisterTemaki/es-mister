param([string]$Prefix='upload-quality-v1',[string]$Script='./validation/quality-script.txt',[string]$Order='0,1',
      [string]$BinaryLabel='stage3-upload-only',[ValidateSet('upload-neon','opaque-fade','copy-pld-pair','alpha-scan')][string]$Knob='upload-neon',
      [ValidateSet('0','1')][string]$UploadNeon='0',[ValidateSet('0','1')][string]$OpaqueFade='0',
      [ValidateSet('0','1')][string]$CopyPldPair='0',[ValidateSet('0','1')][string]$AlphaScan='0',[switch]$FixedDt)
$ErrorActionPreference='Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
if ($Prefix -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Invalid prefix' }
function Bench([string[]]$Arguments) {
    & wsl -d Ubuntu-26.04 -- python3 ./validation/target_run.py @Arguments
    if ($LASTEXITCODE -ne 0) { throw ('Benchmark failed: '+($Arguments -join ' ')) }
}
$root='/media/fat/emulationstation/codex-performance-20261113'
$choices=@($Order.Split(','));$labels=@();$index=0
if ($choices.Count -ne 2 -or @($choices | Where-Object { $_ -notin @('0','1') }).Count) { throw 'Expected two 0/1 choices' }
try {
    foreach ($option in $choices) {
        $index++;$label="$Prefix-$index-v$option";$labels+=$label
        $assert="from pathlib import Path;assert not Path('$root/$label/pid').exists(),'Use a fresh label'"
        $assert | ssh -T -i 'C:/Users/USER/.ssh/id_ed25519_mister' -o BatchMode=yes -o ConnectTimeout=8 root@192.168.15.14 'python3 -'
        if ($LASTEXITCODE -ne 0) { throw 'Preflight failed' }
        $options=@('run',$label,'--binary-label',$BinaryLabel,'--check','--script',$Script)
        if ($FixedDt) { $options+='--quality-fixed-dt' }
        $flags=@{'upload-neon'=$UploadNeon;'opaque-fade'=$OpaqueFade;'copy-pld-pair'=$CopyPldPair;'alpha-scan'=$AlphaScan}
        $flags[$Knob]=$option
        foreach ($name in @($flags.Keys | Sort-Object)) { $options+=@("--$name",$flags[$name]) }
        Bench $options
        $timer=[Diagnostics.Stopwatch]::StartNew()
        $finished=$false
        while ($timer.Elapsed.TotalSeconds -lt 150) {
            $query="from pathlib import Path;p=Path('$root/$label');print(not Path('/proc/'+(p/'pid').read_text().strip()).exists() and (p/'memory.json').exists())"
            $result=$query | ssh -T -i 'C:/Users/USER/.ssh/id_ed25519_mister' -o BatchMode=yes -o ConnectTimeout=8 root@192.168.15.14 'python3 -'
            if ($LASTEXITCODE -eq 0 -and ($result -join '').Trim() -eq 'True') { $finished=$true;break }
            Start-Sleep -Seconds 5
        }
        if (-not $finished) { throw "Quality test timeout: $label" }
        Bench @('collect',$label)
    }
    $code=@'
import pathlib,hashlib,json,re,sys
p=pathlib.Path('validation/target-results');a=p/sys.argv[2];b=p/sys.argv[3]
result={}
for d in (a,b):
 text=(d/'stats.txt').read_text();checks=re.findall(r'(\d+) of (\d+) checked wrong',text)
 assert checks and all(int(wrong)==0 for wrong,total in checks),(d.name,checks)
 result[d.name]=checks
names=sorted(f.name for f in a.glob('*.png'));assert len(names)>=4 and names==sorted(f.name for f in b.glob('*.png'))
for name in names:
 assert hashlib.sha256((a/name).read_bytes()).digest()==hashlib.sha256((b/name).read_bytes()).digest(),'Pixels differ: '+name
 result[name]='identical SHA-256'
(p/(sys.argv[1]+'-summary.json')).write_text(json.dumps(result,indent=2));print(json.dumps(result))
'@
    $code | wsl -d Ubuntu-26.04 -- python3 - $Prefix $labels[0] $labels[1]
    if ($LASTEXITCODE -ne 0) { throw 'Quality comparison failed; do not run candidate performance tests' }
} finally {
    Bench @('restore')
}
