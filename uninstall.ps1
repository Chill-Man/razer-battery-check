$ErrorActionPreference = 'Stop'
$destination = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Programs\BasiliskPulse'))
$expected = [IO.Path]::GetFullPath((Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Programs\BasiliskPulse'))
if ($destination -ne $expected) { throw 'Unexpected installation path' }
$executable = Join-Path $destination 'BasiliskPulse.exe'
Get-Process -Name BasiliskPulse -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $executable } | Stop-Process
Remove-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name BasiliskPulse -ErrorAction SilentlyContinue
$shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'Basilisk Pulse.lnk'
if (Test-Path -LiteralPath $shortcut) { Remove-Item -LiteralPath $shortcut }
if (Test-Path -LiteralPath $executable) { Remove-Item -LiteralPath $executable }
if ((Test-Path -LiteralPath $destination) -and !(Get-ChildItem -LiteralPath $destination -Force)) { Remove-Item -LiteralPath $destination }
Write-Output 'Uninstalled. History and preferences are preserved in %LOCALAPPDATA%\BasiliskPulse and HKCU\Software\BasiliskPulse.'
