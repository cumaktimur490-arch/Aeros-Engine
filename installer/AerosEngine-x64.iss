; Aeros Engine — Полный установщик x64
; Требует Inno Setup 6.x (https://jrsoftware.org/isinfo.php)
; Сборка: iscc AerosEngine-x64.iss /DAppVersion=1.0.0

#define AppVersion "1.0.0"
#define Arch "x64"
#include "common.iss"

[Setup]
AppId={{#AppId}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
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
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
CloseApplications=yes
RestartApplications=no
; Красивый современный инсталлятор
DisableWelcomePage=no
DisableDirPage=no
DisableProgramGroupPage=no
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
Name: "quicklaunchicon"; Description: "{cm:CreateQuickLaunchIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked; OnlyBelowVersion: 6.1

[Files]
; Основной бинарь — берём arch-specific если есть, иначе fallback main.exe
Source: "..\aeros\bin\main-x64.exe"; DestDir: "{app}"; DestName: "{#AppExeName}"; Flags: ignoreversion; Check: FileExists(ExpandConstant('..\aeros\bin\main-x64.exe'))
Source: "..\aeros\bin\main.exe"; DestDir: "{app}"; DestName: "{#AppExeName}"; Flags: ignoreversion; Check: not FileExists(ExpandConstant('..\aeros\bin\main-x64.exe'))
; DLL — все что есть в bin
Source: "..\aeros\bin\*.dll"; DestDir: "{app}"; Flags: ignoreversion
; Модели STL — примеры
Source: "..\aeros\bin\*.stl"; DestDir: "{app}\models"; Flags: ignoreversion
Source: "..\aeros\2.stl"; DestDir: "{app}\models"; Flags: ignoreversion; DestName: "2.stl"
; Run helper
Source: "..\aeros\bin\run.bat"; DestDir: "{app}"; Flags: ignoreversion
; Лицензия и ридми
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; DestName: "README.txt"; Flags: ignoreversion
; Версия
Source: "..\VERSION"; DestDir: "{app}"; DestName: "VERSION.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\{#AppExeName}"; Comment: "{#AppName} {#ArchTitle}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon; WorkingDir: "{app}"; IconFilename: "{app}\{#AppExeName}"
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: quicklaunchicon; WorkingDir: "{app}"

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}\logs"
Type: files; Name: "{app}\imgui.ini"

[Code]
function InitializeSetup(): Boolean;
begin
  // Проверка Windows 10+
  if not IsWin64 then
  begin
    MsgBox('Aeros Engine x64 требует 64-битную Windows 10 или новее.' + #13#10 + 'Для 32-битной системы используйте Aeros-Engine-Setup-x86.', mbError, MB_OK);
    Result := False;
  end else
    Result := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    // Создаём файл с информацией об установке
    SaveStringToFile(ExpandConstant('{app}\install-info.txt'),
      'Aeros Engine ' + ExpandConstant('{#AppVersion}') + ' {#ArchTitle}' + #13#10 +
      'Installed: ' + GetDateTimeString('yyyy-mm-dd hh:nn:ss', '-', ':') + #13#10 +
      'Arch: {#ArchSuffix}' + #13#10 +
      'Path: ' + ExpandConstant('{app}') + #13#10,
      False);
  end;
end;
