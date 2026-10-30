param([string]$GameBin32 = 'D:\SteamLibrary\steamapps\common\Resident Evil 4\Bin32')
$ErrorActionPreference = 'Stop'
$exe = Join-Path $GameBin32 'bio4.exe'
$state = Join-Path $GameBin32 'RE4OGAimWalk'
$list = Join-Path $state 'installed.tsv'
if (-not (Test-Path -LiteralPath $exe)) { throw "bio4.exe not found in $GameBin32" }
$running = Get-Process bio4 -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }
if ($running) { throw 'Close Resident Evil 4 before uninstalling.' }
if (-not (Test-Path -LiteralPath $list)) { throw "Install manifest not found: $list" }
foreach ($line in Get-Content -LiteralPath $list) {
  $parts = $line -split "`t"
  if ($parts.Count -lt 2 -or $parts[0] -eq 'file') { continue }
  $target = Join-Path $GameBin32 $parts[0]
  if (-not (Test-Path -LiteralPath $target)) { continue }
  $hash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
  if ($hash -eq $parts[1]) { Remove-Item -LiteralPath $target }
  else { Write-Output "Kept changed file: $target" }
}
$log = Join-Path $GameBin32 'RE4OGAimWalk.log'
if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log }
Remove-Item -LiteralPath $list
if (-not (Get-ChildItem -LiteralPath $state -Force)) { Remove-Item -LiteralPath $state }
