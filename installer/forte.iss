; Forte's Windows installer (issue 0020, SPEC §9.4). Built by tools/release/package.ps1, which
; passes the version and the staged package folder:
;   ISCC.exe /DAppVersion=0.9.0 /DSourceDir=<staged folder> /DOutputDir=<dist> installer\forte.iss
; Per-machine, as admin. The VST3 goes to Common Files\VST3, the standalone app to Program
; Files\Forte with a Start-menu shortcut; either can be left out. Never touches
; Documents\Forte\Presets, so uninstalling keeps the user's presets.

#ifndef AppVersion
  #error Pass /DAppVersion=x.y.z
#endif
#ifndef SourceDir
  #error Pass /DSourceDir=<the staged package folder>
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{7C3F2A61-5E0B-4D7A-9B1F-2F6E3C8D4A90}
AppName=Forte
AppVersion={#AppVersion}
AppVerName=Forte {#AppVersion}
AppPublisher=The Forte project
AppPublisherURL=https://github.com/sammikinsrox/forte
AppSupportURL=https://github.com/sammikinsrox/forte/issues
DefaultDirName={autopf}\Forte
DisableProgramGroupPage=yes
DisableDirPage=yes
LicenseFile={#SourceDir}\LICENSE
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=Forte-{#AppVersion}-windows-x64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Forte {#AppVersion}
UninstallDisplayIcon={app}\Forte.exe

[Types]
Name: "full"; Description: "VST3 plugin and standalone app"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (C:\Program Files\Common Files\VST3)"; Types: full custom
Name: "standalone"; Description: "Standalone app (C:\Program Files\Forte)"; Types: full custom

[Files]
Source: "{#SourceDir}\Forte.vst3\*"; DestDir: "{commoncf64}\VST3\Forte.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\Forte.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#SourceDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\THIRD-PARTY.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\CREDITS.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\README.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Forte"; Filename: "{app}\Forte.exe"; Components: standalone
