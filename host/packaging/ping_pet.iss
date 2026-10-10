; 윈도우 설치 파일 (Inno Setup 6) — PyInstaller 폴더 "dist\Ping Pet" 를 묶는다
;   host 폴더에서 : iscc /DAppVersion=0.1.0 packaging\ping_pet.iss
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{6B0C3F7E-6C1A-4F0B-9E57-5A0E2B7C1D11}
AppName=Ping Pet
AppVersion={#AppVersion}
AppPublisher=Ping Pet
DefaultDirName={autopf}\Ping Pet
DefaultGroupName=Ping Pet
OutputDir=dist
OutputBaseFilename=PingPet-{#AppVersion}-windows-Setup
SetupIconFile=build\icon.ico
UninstallDisplayIcon={app}\Ping Pet.exe
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=lowest
WizardStyle=modern

[Languages]
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"

[Tasks]
Name: "autostart"; Description: "로그인할 때 실행"; GroupDescription: "추가 작업:"

[Files]
Source: "dist\Ping Pet\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\Ping Pet"; Filename: "{app}\Ping Pet.exe"
Name: "{group}\Ping Pet 제거"; Filename: "{uninstallexe}"
Name: "{userstartup}\Ping Pet"; Filename: "{app}\Ping Pet.exe"; Tasks: autostart

[Run]
Filename: "{app}\Ping Pet.exe"; Description: "Ping Pet 실행"; Flags: nowait postinstall skipifsilent
