param([string]$LongPrefix='opaque-long-v1',[string]$StartupPrefix='startup-upload-v1')
$ErrorActionPreference='Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
if ($LongPrefix -notmatch '^[a-zA-Z0-9_-]+$' -or $StartupPrefix -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Invalid prefix' }
$timer=[Diagnostics.Stopwatch]::StartNew()
while ($timer.Elapsed.TotalMinutes -lt 65) {
    $text=Get-Content -LiteralPath "./validation/$LongPrefix.log" -Raw
    if ([regex]::Matches($text,"COMPLETE $LongPrefix-").Count -eq 4 -and $text.Contains('original launcher restored')) { break }
    Start-Sleep -Seconds 10
}
if ($timer.Elapsed.TotalMinutes -ge 65) { throw 'Long comparison did not finish; no subsequent test started' }
& wsl -d Ubuntu-26.04 -- python3 ./validation/target_run.py upload ./validation/arm/emulationstation-upload-only-stripped stage3-upload-only
if ($LASTEXITCODE -ne 0) { throw 'Upload failed' }
& pwsh -NoProfile -File ./validation/upload_quality.ps1 -Prefix upload-quality-v1
if ($LASTEXITCODE -ne 0) { throw 'Upload quality failed' }
& pwsh -NoProfile -File ./validation/startup_compare.ps1 -BinaryLabel stage3-upload-only -Prefix $StartupPrefix -WorkerOrder 4,4,4,4,4,4,4,4,4,4,4,4 -UploadOrder 0,1,1,0,0,1,1,0,0,1,1,0 -InputProbe
if ($LASTEXITCODE -ne 0) { throw 'Startup comparison failed' }
Write-Output 'POST-LONG UPLOAD QUALITY AND STARTUP COMPLETE'
