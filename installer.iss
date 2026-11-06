; Installer fuer Tekkforge. Wird von GitHub beim Veroeffentlichen gebaut.
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{7C1E7C1A-4B1B-4B6C-9C52-1F0A7E2D5A01}
AppName=Tekkforge
AppVersion={#AppVersion}
AppPublisher=Tekkbox
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
Uninstallable=yes
UninstallDisplayName=Tekkforge
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=dist
OutputBaseFilename=Tekkforge-Setup
Compression=lzma2
SolidCompression=yes
; Schliesst FL Studio nach Rueckfrage, falls das Plugin gerade geladen ist
CloseApplications=yes
CloseApplicationsFilter=*.vst3,*.dll
RestartApplications=no

[Languages]
Name: "de"; MessagesFile: "compiler:Languages\German.isl"

[Files]
Source: "build\Tekkforge_artefacts\Release\VST3\Tekkforge.vst3\*"; DestDir: "{commoncf64}\VST3\Tekkforge.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[Messages]
FinishedLabel=[name] ist installiert. Starte FL Studio neu, dann läuft die neue Version.
