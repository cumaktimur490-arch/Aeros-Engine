; Aeros Engine — Полный установщик x86 (32-bit)
; Требует Inno Setup 6.x
; Сборка: iscc AerosEngine-x86.iss /DAppVersion=1.2.1

#ifndef AppVersion
  #define AppVersion "1.2.1"
#endif
#define Arch "x86"
#include "common.iss"

[Setup]
AppId={{#AppId}
AppName={#AppName} (32-bit)
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={autopf}\{#AppName} x86
DefaultGroupName={#AppName} x86
AllowNoIcons=yes
LicenseFile=..\LICENSE
OutputDir=..\release
OutputBaseFilename={#OutputBaseName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
#if FileExists("..\aeros\bin\icon.ico")
SetupIconFile=..\aeros\bin\icon.ico
#endif
UninstallDisplayIcon={app}\{#AppExeName}
; Для x86 разрешаем установку на 32 и 64 битных системах
ArchitecturesAllowed=x86compatible x64compatible
MinVersion=10.0
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
CloseApplications=yes
RestartApplications=no
DisableWelcomePage=no
Uninstallable=yes
CreateUninstallRegKey=yes
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} Setup {#ArchTitle}
VersionInfoCopyright=Copyright (C) 2026 Aeros Team
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\aeros\bin\main-x86.exe"; DestDir: "{app}"; DestName: "{#AppExeName}"; Flags: ignoreversion; Check: FileExists(ExpandConstant('..\aeros\bin\main-x86.exe'))
Source: "..\aeros\bin\main.exe"; DestDir: "{app}"; DestName: "{#AppExeName}"; Flags: ignoreversion; Check: not FileExists(ExpandConstant('..\aeros\bin\main-x86.exe'))
Source: "..\aeros\bin\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\aeros\bin\*.stl"; DestDir: "{app}\models"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\aeros\2.stl"; DestDir: "{app}\models"; DestName: "2.stl"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\aeros\bin\run.bat"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\README.md"; DestDir: "{app}"; DestName: "README.txt"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\VERSION"; DestDir: "{app}"; DestName: "VERSION.txt"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\{#AppName} {#ArchTitle}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName} {#ArchTitle}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon; WorkingDir: "{app}"

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    SaveStringToFile(ExpandConstant('{app}\install-info.txt'),
      'Aeros Engine ' + ExpandConstant('{#AppVersion}') + ' {#ArchTitle}' + #13#10 +
      'Installed: ' + GetDateTimeString('yyyy-mm-dd hh:nn:ss', '-', ':') + #13#10 +
      'Arch: {#ArchSuffix}' + #13#10,
      False);
  end;
end;
