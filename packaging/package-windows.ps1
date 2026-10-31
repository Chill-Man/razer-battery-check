param([ValidateSet('x86','x64','arm64')][string]$Architecture='x64',[switch]$SkipBuild)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
if(!$SkipBuild){ & "$root\build.ps1" -Architecture $Architecture }
$version=(Get-Content -LiteralPath "$root\VERSION" -Raw).Trim()
$stage=Join-Path $root "build\packages\windows-$Architecture"
New-Item -ItemType Directory -Path $stage,"$root\release" -Force | Out-Null
Copy-Item -LiteralPath "$root\dist\$Architecture\BasiliskPulse.exe","$root\LICENSE" -Destination $stage -Force
Copy-Item -LiteralPath "$root\docs\QUICKSTART-WINDOWS.txt" -Destination "$stage\README.txt" -Force
Compress-Archive -LiteralPath "$stage\BasiliskPulse.exe","$stage\LICENSE","$stage\README.txt" -DestinationPath "$root\release\BasiliskPulse-$version-windows-$Architecture-portable.zip" -CompressionLevel Optimal -Force
$iscc=Get-Command ISCC.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
if(!$iscc){$iscc=Get-ChildItem 'C:\Program Files (x86)\Inno Setup *\ISCC.exe','C:\Program Files\Inno Setup *\ISCC.exe' -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName}
if(!$iscc){throw 'Inno Setup compiler is required to build Setup installers.'}
Push-Location "$root\packaging\windows"
try { & $iscc "/DArch=$Architecture" setup.iss; if($LASTEXITCODE){throw 'Installer build failed'} } finally {Pop-Location}
# Verify the PE target inside the portable build before publishing it.
$stream=[IO.File]::OpenRead("$stage\BasiliskPulse.exe");$reader=[IO.BinaryReader]::new($stream)
try{$stream.Position=0x3c;$offset=$reader.ReadInt32();$stream.Position=$offset+4;$machine=$reader.ReadUInt16()}finally{$reader.Dispose();$stream.Dispose()}
$expected=@{x86=0x14c;x64=0x8664;arm64=0xaa64}[$Architecture]
if($machine -ne $expected){throw "Wrong PE architecture: $machine"}
Write-Output "Packaged Windows $Architecture: PE machine 0x$($machine.ToString('x'))"
