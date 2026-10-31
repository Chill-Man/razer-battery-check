$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Install Visual Studio Build Tools with Desktop development with C++.' }
$compiler = Get-ChildItem "$vs\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe" | Sort-Object FullName -Descending | Select-Object -First 1
$msvc = Split-Path (Split-Path (Split-Path (Split-Path $compiler.FullName)))
$sdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
$sdk = Get-ChildItem "$sdkRoot\Include" -Directory | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty Name
$env:INCLUDE = "$msvc\include;$sdkRoot\Include\$sdk\ucrt;$sdkRoot\Include\$sdk\shared;$sdkRoot\Include\$sdk\um;$sdkRoot\Include\$sdk\winrt"
$env:LIB = "$msvc\lib\x64;$sdkRoot\Lib\$sdk\ucrt\x64;$sdkRoot\Lib\$sdk\um\x64"
$env:PATH = "$(Split-Path $compiler.FullName);$sdkRoot\bin\$sdk\x64;$env:PATH"
New-Item -ItemType Directory "$root\dist","$root\build" -Force | Out-Null
Push-Location $root
try {
    if (!(Test-Path -LiteralPath "$root\src\pulse.ico")) { & "$root\make-icon.ps1" }
    & "$sdkRoot\bin\$sdk\x64\rc.exe" /nologo /fo build\app.res /i src src\app.rc
    if ($LASTEXITCODE) { throw 'Resource build failed' }
    & $compiler.FullName /nologo /std:c++17 /utf-8 /EHsc /O2 /MT /W4 /DUNICODE /D_UNICODE src\main.cpp src\battery.cpp build\app.res /Fo:build\ /Fe:dist\BasiliskPulse.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:app.manifest user32.lib gdi32.lib shell32.lib setupapi.lib hid.lib d2d1.lib dwrite.lib dwmapi.lib windowscodecs.lib advapi32.lib ole32.lib
    if ($LASTEXITCODE) { throw 'Application build failed' }
    & $compiler.FullName /nologo /std:c++17 /utf-8 /EHsc /O2 /MT /W4 tests\tests.cpp src\battery.cpp /Fo:build\ /Fe:build\tests.exe /link setupapi.lib hid.lib shell32.lib ole32.lib
    if ($LASTEXITCODE) { throw 'Test build failed' }
    & "$root\build\tests.exe"
    if ($LASTEXITCODE) { throw 'Tests failed' }
} finally { Pop-Location }
