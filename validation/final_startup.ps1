param([string]$Prefix='startup-full-warm-v1',[switch]$Reboot)
$ErrorActionPreference='Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
if ($Prefix -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Invalid prefix' }
$index=0
foreach ($variant in @('reference','candidate','candidate','reference')) {
    $index++;$label="$Prefix-$index-$variant"
    $enabled=if($variant -eq 'candidate'){'1'}else{'0'}
    $systems=if($variant -eq 'candidate'){'2'}else{'4'}
    $textures=if($variant -eq 'candidate'){'1'}else{'2'}
    $sha=if($variant -eq 'candidate'){'872dd340826f39da1bb14371681bb0decc4f4d3c640ae93e646ef46337465b55'}else{'1c29852e682db65a01590674986e00c937ee14a63dc44d053700ff4a325393a6'}
    if ($Reboot) {
        & wsl -d Ubuntu-26.04 -- python3 ./validation/target_lifecycle.py reboot $label
        if ($LASTEXITCODE -ne 0) { throw 'Linux reboot not confirmed; do not advance' }
    }
    $options=@('-Prefix',$label,'-BinaryLabel',"stage3-final-$variant",'-WorkerOrder',$systems,'-TextureThreads',$textures,'-UploadNeon',$enabled,'-OpaqueFade',$enabled,'-CopyPldPair',$enabled,'-InputProbe','-Wrapper')
    if ($Reboot) { $options+=@('-ColdStart','-ExpectedBinarySha',$sha) }
    & pwsh -NoProfile -File ./validation/startup_compare.ps1 @options
    if ($LASTEXITCODE -ne 0) { throw 'Startup first-interaction test failed' }
}
