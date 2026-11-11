[Setup]
AppId={{2501B7DF-576E-4A2E-9C2E-B3EBEF85E02F}
AppName=OmniCore-SDR-OnAir
AppVersion={#AppVersion}
AppPublisher=Liren66
AppPublisherURL=https://github.com/Liren66/OmniCore-SDR-OnAir
DefaultDirName={localappdata}\Programs\OmniCore-SDR-OnAir
DefaultGroupName=OmniCore-SDR-OnAir
AllowNoIcons=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=lowest
MinVersion=10.0
OutputDir={#ReleaseDir}
OutputBaseFilename=OmniCore-SDR-OnAir-{#AppVersion}-windows-x64-setup
VersionInfoVersion={#AppVersion}.0
SetupIconFile=OnAir.ico
UninstallDisplayIcon={app}\OmniCore-SDR-OnAir.exe
LicenseFile={#ReleaseDir}\OmniCore-SDR-OnAir-{#AppVersion}-windows-x64\LICENSE.txt
Compression=lzma2/normal
SolidCompression=yes
CloseApplications=yes
RestartApplications=no

[Files]
Source: "{#ReleaseDir}\OmniCore-SDR-OnAir-{#AppVersion}-windows-x64\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\OmniCore-SDR-OnAir"; Filename: "{app}\OmniCore-SDR-OnAir.exe"; WorkingDir: "{app}"