; Aeros Engine — Полный установщик arm64 (ARM64)
; Требует Inno Setup 6.x (https://jrsoftware.org/isinfo.php)
; Сборка: iscc AerosEngine-arm64.iss /DAppVersion=1.8.0

#ifndef AppVersion
  #define AppVersion "1.8.0"
#endif
#define Arch "arm64"
#include "common.iss"

[Setup]
AppId={{#AppId}
AppName={#AppName} ({#ArchTitle})
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={autopf}\{#AppName} {#ArchTitle}
DefaultGroupName={#AppName} {#ArchTitle}
AllowNoIcons=yes
LicenseFile=..\LICENSE
OutputDir=..\release
OutputBaseFilename={#OutputBaseName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
; Иконка установщика. ..\aeros\bin\icon.ico в репозитории нет (bin/ в .gitignore),
; настоящий icon.ico лежит в aeros\src — иначе setup.exe собирался без иконки.
SetupIconFile=..\aeros\src\icon.ico
UninstallDisplayIcon={app}\{#AppExeName}
ArchitecturesAllowed=arm64 x64compatible
ArchitecturesInstallIn64BitMode=arm64 x64compatible
MinVersion=10.0
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
CloseApplications=yes
RestartApplications=no
DisableWelcomePage=no
DisableDirPage=no
DisableProgramGroupPage=no
Uninstallable=yes
CreateUninstallRegKey=yes
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} Setup {#ArchTitle}
VersionInfoCopyright=Copyright (C) 2026 GoGonam AoS.
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
; Бинарь СТРОГО своей архитектуры и без fallback на общий bin\main.exe:
; если файла нет, ISCC упадёт при компиляции, а не положит чужую архитектуру
; (fallback и давал «CreateProcess сбой; код 216» на ПК пользователя).
Source: "..\aeros\bin\main-arm64.exe"; DestDir: "{app}"; DestName: "{#AppExeName}"; Flags: ignoreversion
; DLL — все что есть в bin
; DLL — только arm64: glfw3.dll из bin является 64-битным и сюда попадать не должен
Source: "..\aeros\bin\glfw3-arm64.dll"; DestDir: "{app}"; DestName: "glfw3.dll"; Flags: ignoreversion skipifsourcedoesntexist
; Модели STL — примеры
Source: "..\aeros\bin\*.stl"; DestDir: "{app}\models"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\aeros\2.stl"; DestDir: "{app}\models"; DestName: "2.stl"; Flags: ignoreversion skipifsourcedoesntexist
; Run helper
Source: "..\aeros\bin\run.bat"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
; Лицензия и ридми
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\README.md"; DestDir: "{app}"; DestName: "README.txt"; Flags: ignoreversion skipifsourcedoesntexist
; Версия
Source: "..\VERSION"; DestDir: "{app}"; DestName: "VERSION.txt"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\{#AppName} {#ArchTitle}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\{#AppExeName}"; Comment: "{#AppName} {#ArchTitle}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName} {#ArchTitle}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon; WorkingDir: "{app}"; IconFilename: "{app}\{#AppExeName}"
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: quicklaunchicon; WorkingDir: "{app}"

[Run]
; Автозапуск — только если установленный exe совпал по архитектуре (иначе код 216)
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent; Check: AerosLaunchAllowed

[UninstallDelete]
Type: filesandordirs; Name: "{app}\logs"
Type: files; Name: "{app}\imgui.ini"

[Code]
#include "archcheck.inc"

function InitializeSetup(): Boolean;
begin
  // Проверка Windows 10+ и ARM64
  Result := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    // Контроль архитектуры установленного exe (защита от CreateProcess код 216)
    AerosArchOK := AerosVerifyInstalledExe('{#AppExeName}');
    SaveStringToFile(ExpandConstant('{app}\install-info.txt'),
      'Aeros Engine ' + ExpandConstant('{#AppVersion}') + ' {#ArchTitle}' + #13#10 +
      'Installed: ' + GetDateTimeString('yyyy-mm-dd hh:nn:ss', '-', ':') + #13#10 +
      'Arch: {#ArchSuffix}' + #13#10 +
      'Path: ' + ExpandConstant('{app}') + #13#10,
      False);
  end;
end;
