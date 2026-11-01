param([string]$GameBin32 = 'D:\SteamLibrary\steamapps\common\Resident Evil 4\Bin32')
$ErrorActionPreference = 'Stop'

if (Get-Process bio4 -ErrorAction SilentlyContinue) { throw 'Close Resident Evil 4 before uninstalling.' }
$proxy = Join-Path $GameBin32 'dinput8.dll'
$coop = Join-Path $GameBin32 'RE4CoopDinput8.dll'
if (-not (Test-Path -LiteralPath $coop)) { throw 'RE4CoopDinput8.dll not found; refusing to remove or replace files.' }
if (Test-Path -LiteralPath $proxy) {
  $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
  Move-Item -LiteralPath $proxy -Destination "$proxy.removed-$stamp.bak"
}
foreach ($name in 're4bettermovement.dll', 'RE4OGAimWalk.asi', 're4bettermovement.asi') {
  $path = Join-Path $GameBin32 $name
  if (Test-Path -LiteralPath $path) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    Move-Item -LiteralPath $path -Destination "$path.removed-$stamp.bak"
  }
}
Move-Item -LiteralPath $coop -Destination $proxy
Write-Output "Restored the original COOP dinput8.dll in $GameBin32."
