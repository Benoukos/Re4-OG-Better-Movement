param([string]$GameBin32 = 'D:\SteamLibrary\steamapps\common\Resident Evil 4\Bin32')
$ErrorActionPreference = 'Stop'

$exe = Join-Path $GameBin32 'bio4.exe'
$coopLoader = Join-Path $GameBin32 'dinput8.dll'
$packageBin32 = Join-Path $PSScriptRoot 'Bin32'
$sourceBin32 = if ((Test-Path -LiteralPath (Join-Path $packageBin32 'dinput8.dll')) -and
                 (Test-Path -LiteralPath (Join-Path $packageBin32 're4bettermovement.dll'))) {
  $packageBin32
} else {
  Join-Path $PSScriptRoot 'build-mingw'
}
$proxySource = Join-Path $sourceBin32 'dinput8.dll'
$pluginSource = Join-Path $sourceBin32 're4bettermovement.dll'
$files = @(
  @{ Source = $proxySource; Name = 'dinput8.dll' },
  @{ Source = $pluginSource; Name = 're4bettermovement.dll' }
)

if (-not (Test-Path -LiteralPath $exe)) { throw "bio4.exe not found in $GameBin32" }
if (-not (Test-Path -LiteralPath $coopLoader)) { throw 'No existing dinput8.dll found; this installer is intended to preserve the COOP loader.' }
if (Test-Path -LiteralPath (Join-Path $GameBin32 'RE4CoopDinput8.dll')) { throw 'RE4CoopDinput8.dll already exists; inspect it before reinstalling.' }
if (Get-Process bio4 -ErrorAction SilentlyContinue) { throw 'Close Resident Evil 4 before installing.' }
foreach ($file in $files) {
  if (-not (Test-Path -LiteralPath $file.Source)) { throw "Build output missing: $($file.Source)" }
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$toBackup = @('RE4OGAimWalk.asi', 're4bettermovement.asi', 're4bettermovement.dll')
foreach ($name in $toBackup) {
  $path = Join-Path $GameBin32 $name
  if (Test-Path -LiteralPath $path) {
    $backup = "$path.before-coop-dll-$stamp.bak"
    Move-Item -LiteralPath $path -Destination $backup
    Write-Output "Backed up $name to $(Split-Path -Leaf $backup)"
  }
}
$coopBackup = Join-Path $GameBin32 'RE4CoopDinput8.dll'
Move-Item -LiteralPath $coopLoader -Destination $coopBackup
Write-Output 'Preserved the original COOP dinput8.dll as RE4CoopDinput8.dll'
foreach ($file in $files) {
  Copy-Item -LiteralPath $file.Source -Destination (Join-Path $GameBin32 $file.Name)
}
Write-Output "Installed dinput8 proxy + re4bettermovement.dll in $GameBin32."
