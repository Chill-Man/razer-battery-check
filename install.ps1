param([switch]$NoLaunch)
$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'dist\BasiliskPulse.exe'
if (!(Test-Path -LiteralPath $source)) { throw 'Run build.ps1 first.' }
$destination = Join-Path $env:LOCALAPPDATA 'Programs\BasiliskPulse'
$executable = Join-Path $destination 'BasiliskPulse.exe'
Get-Process -Name BasiliskPulse -ErrorAction SilentlyContinue | Where-Object { $_.Path -in @($source,$executable) } | ForEach-Object {
    Stop-Process -Id $_.Id -Force
    if (!$_.WaitForExit(5000)) { throw 'The previous instance did not stop in time.' }
}
New-Item -ItemType Directory -Path $destination -Force | Out-Null
Copy-Item -LiteralPath $source -Destination $executable -Force
$shortcutPath = Join-Path ([Environment]::GetFolderPath('Programs')) 'Basilisk Pulse.lnk'
$shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut($shortcutPath)
$shortcut.TargetPath = $executable
$shortcut.Arguments = '--show'
$shortcut.WorkingDirectory = $destination
$shortcut.Description = 'Заряд Razer Basilisk V3 Pro и прогноз времени работы'
$shortcut.IconLocation = "$executable,0"
$shortcut.Save()
# If the user enabled autostart in a portable build, keep it pointing at the installed binary.
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
if ((Get-ItemProperty -LiteralPath $runKey -Name BasiliskPulse -ErrorAction SilentlyContinue).BasiliskPulse) {
    Set-ItemProperty -LiteralPath $runKey -Name BasiliskPulse -Value ('"' + $executable + '"')
}
if (!$NoLaunch) { Start-Process -FilePath $executable -WindowStyle Hidden }
Write-Output "Installed: $executable"
Write-Output "Start menu: $shortcutPath"
