param([ValidateSet('x86','x64','arm64')][string]$Architecture='x64')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -property installationPath
if (!$vs) { throw 'Install Visual Studio Build Tools with Desktop development with C++.' }
$hostTarget = if ([Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq 'Arm64') {'arm64'} else {'x64'}
$compiler = Get-ChildItem "$vs\VC\Tools\MSVC\*\bin\Host$hostTarget\$Architecture\cl.exe" -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
if (!$compiler) { $compiler = Get-ChildItem "$vs\VC\Tools\MSVC\*\bin\Hostx64\$Architecture\cl.exe" -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1 }
if (!$compiler) { throw "MSVC compiler for $Architecture is not installed." }
$msvc = Split-Path (Split-Path (Split-Path (Split-Path $compiler.FullName)))
$sdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
$sdk = Get-ChildItem "$sdkRoot\Include" -Directory | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty Name
$env:INCLUDE = "$msvc\include;$sdkRoot\Include\$sdk\ucrt;$sdkRoot\Include\$sdk\shared;$sdkRoot\Include\$sdk\um;$sdkRoot\Include\$sdk\winrt"
if (!(Test-Path "$msvc\lib\$Architecture\libcmt.lib")) { throw "MSVC C++ libraries for $Architecture are not installed." }
$env:LIB = "$msvc\lib\$Architecture;$sdkRoot\Lib\$sdk\ucrt\$Architecture;$sdkRoot\Lib\$sdk\um\$Architecture"
$sdkBin = "$sdkRoot\bin\$sdk\$hostTarget"
if (!(Test-Path "$sdkBin\rc.exe")) {$sdkBin="$sdkRoot\bin\$sdk\x64"}
$env:PATH = "$(Split-Path $compiler.FullName);$sdkBin;$env:PATH"
$outputDir = "$root\dist\$Architecture"
$objectDir = "$root\build\$Architecture"
New-Item -ItemType Directory $outputDir,$objectDir -Force | Out-Null
Push-Location $root
try {
    if (!(Test-Path -LiteralPath "$root\src\pulse.ico")) { & "$root\make-icon.ps1" }
    & "$sdkBin\rc.exe" /nologo /fo "$objectDir\app.res" /i src src\app.rc
    if ($LASTEXITCODE) { throw 'Resource build failed' }
    & $compiler.FullName /nologo /std:c++17 /utf-8 /EHsc /O2 /MT /W4 /DUNICODE /D_UNICODE src\main.cpp src\battery_core.cpp src\battery_windows.cpp "$objectDir\app.res" "/Fo:$objectDir\" "/Fe:$outputDir\BasiliskPulse.exe" /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:app.manifest user32.lib gdi32.lib shell32.lib setupapi.lib hid.lib d2d1.lib dwrite.lib dwmapi.lib windowscodecs.lib advapi32.lib ole32.lib
    if ($LASTEXITCODE) { throw 'Application build failed' }
    & $compiler.FullName /nologo /std:c++17 /utf-8 /EHsc /O2 /MT /W4 tests\tests.cpp src\battery_core.cpp src\battery_windows.cpp "/Fo:$objectDir\" "/Fe:$objectDir\tests.exe" /link setupapi.lib hid.lib shell32.lib ole32.lib
    if ($LASTEXITCODE) { throw 'Test build failed' }
    & "$objectDir\tests.exe"
    if ($LASTEXITCODE) { throw 'Tests failed' }
    if ($Architecture -eq 'x64') {
        try { Copy-Item -LiteralPath "$outputDir\BasiliskPulse.exe" -Destination "$root\dist\BasiliskPulse.exe" -Force } catch { Write-Warning 'Legacy EXE is in use; architecture-specific output is ready.' }
        Copy-Item -LiteralPath "$objectDir\tests.exe" -Destination "$root\build\tests.exe" -Force
    }
    Write-Output "Built and tested: $outputDir\BasiliskPulse.exe"
} finally { Pop-Location }
