; Inno Setup script for the Windows settings app.
; ISCC is invoked with /DAppVersion=1.0.0-N from packaging/deb/changelog.
; The script is stored as UTF-8. package-windows.ps1 writes a BOM copy
; next to this file before compiling, so Russian strings stay intact.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{8F4C2A91-6B0E-4D37-9C15-2A7E5B0D4F18}
AppName=Ракета
AppVersion={#AppVersion}
AppVerName=Ракета {#AppVersion}
AppPublisher=Raketa
DefaultDirName={localappdata}\Raketa
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
OutputDir=..\..\dist
OutputBaseFilename=rocket-switcher_{#AppVersion}_windows-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\rocket-switcher-config.exe
CloseApplications=no

[Files]
Source: "..\..\dist\windows\stage\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{autoprograms}\Ракета"; Filename: "{app}\rocket-switcher-config.exe"
Name: "{userdesktop}\Ракета"; Filename: "{app}\rocket-switcher-config.exe"; Tasks: desktopicon

[Tasks]
Name: desktopicon; Description: "Ярлык на рабочем столе"; Flags: unchecked

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Raketa"; ValueData: """{app}\rocket-switcher-config.exe"" --tray"; Flags: uninsdeletevalue

[Run]
Filename: "{app}\rocket-switcher-config.exe"; Description: "Запустить Ракету"; Flags: nowait postinstall skipifsilent
