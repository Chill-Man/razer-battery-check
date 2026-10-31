#ifndef Arch
  #define Arch "x64"
#endif
#define Version Trim(FileRead(FileOpen("..\..\VERSION")))

[Setup]
AppId={{229A2BA6-076E-4CF4-AC15-26C3064E91CD}
AppName=Basilisk Pulse
AppVersion={#Version}
AppPublisher=Chill-Man
AppPublisherURL=https://github.com/Chill-Man/razer-battery-check
AppSupportURL=https://github.com/Chill-Man/razer-battery-check/issues
DefaultDirName={localappdata}\Programs\BasiliskPulse
DefaultGroupName=Basilisk Pulse
PrivilegesRequired=lowest
MinVersion=10.0.14393
WizardStyle=modern
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
SetupIconFile=..\..\src\pulse.ico
UninstallDisplayIcon={app}\BasiliskPulse.exe
OutputDir=..\..\release
OutputBaseFilename=BasiliskPulse-{#Version}-windows-{#Arch}-setup
Compression=lzma2/ultra64
SolidCompression=yes
AppMutex=Local\BasiliskPulse.SingleInstance
CloseApplications=yes
RestartApplications=no
#if Arch == "x64"
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#elif Arch == "arm64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "..\..\dist\{#Arch}\BasiliskPulse.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\LICENSE"; DestDir: "{app}"
Source: "..\..\docs\QUICKSTART-WINDOWS.txt"; DestDir: "{app}"; DestName: "README.txt"

[Icons]
Name: "{userprograms}\Basilisk Pulse"; Filename: "{app}\BasiliskPulse.exe"; Parameters: "--show"
Name: "{userdesktop}\Basilisk Pulse"; Filename: "{app}\BasiliskPulse.exe"; Parameters: "--show"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "BasiliskPulse"; Flags: uninsdeletevalue

[Run]
Filename: "{app}\BasiliskPulse.exe"; Description: "Open Basilisk Pulse"; Flags: nowait postinstall skipifsilent
