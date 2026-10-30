param([string]$GameBin32 = 'D:\SteamLibrary\steamapps\common\Resident Evil 4\Bin32')
$ErrorActionPreference = 'Stop'
$exe = Join-Path $GameBin32 'bio4.exe'
$asi = @((Join-Path $PSScriptRoot 'dist\RE4OGAimWalk.asi'), (Join-Path $PSScriptRoot 'build-mingw\RE4OGAimWalk.asi'), (Join-Path $PSScriptRoot 'build\Release\RE4OGAimWalk.asi'), (Join-Path $PSScriptRoot 'build\RE4OGAimWalk.asi')) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
$loader = Join-Path $PSScriptRoot 'third_party\ultimate-asi-loader\dinput8.dll'
if (-not (Test-Path -LiteralPath $exe)) { throw "bio4.exe not found in $GameBin32" }
if (-not (Test-Path -LiteralPath $asi)) { throw "Build output missing: $asi" }
if (-not (Test-Path -LiteralPath $loader)) { throw "ASI loader missing: $loader" }
$bytes = [IO.File]::ReadAllBytes($exe)
$pe = [BitConverter]::ToInt32($bytes, 0x3c)
$stamp = [BitConverter]::ToUInt32($bytes, $pe + 8)
if ($stamp -ne 0x5A6192E5) { throw ('Unsupported bio4.exe timestamp: 0x{0:X8}' -f $stamp) }
$running = Get-Process bio4 -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }
if ($running) { throw 'Close Resident Evil 4 before installing.' }
foreach ($name in 'dinput8.dll', 'RE4OGAimWalk.asi') {
  if (Test-Path -LiteralPath (Join-Path $GameBin32 $name)) { throw "Will not overwrite existing $name; inspect the existing mod or loader first." }
}
$state = Join-Path $GameBin32 'RE4OGAimWalk'
if (Test-Path -LiteralPath $state) { throw "Install state already exists: $state" }
New-Item -ItemType Directory -Path $state | Out-Null
Copy-Item -LiteralPath $loader -Destination (Join-Path $GameBin32 'dinput8.dll')
Copy-Item -LiteralPath $asi -Destination (Join-Path $GameBin32 'RE4OGAimWalk.asi')
$rows = @("file`tsha256")
foreach ($name in 'dinput8.dll', 'RE4OGAimWalk.asi') {
  $hash = (Get-FileHash -LiteralPath (Join-Path $GameBin32 $name) -Algorithm SHA256).Hash
  $rows += "$name`t$hash"
}
Set-Content -LiteralPath (Join-Path $state 'installed.tsv') -Value $rows -Encoding utf8
"Installed RE4 OG Aim Walk into $GameBin32"


