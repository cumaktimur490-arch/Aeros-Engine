; Aeros Engine — Полный установщик x86 (32-bit)
; Требует Inno Setup 6.x
; Сборка: iscc AerosEngine-x86.iss /DAppVersion=1.8.0

#ifndef AppVersion
  #define AppVersion "1.8.0"
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
; Иконка установщика. ..\aeros\bin\icon.ico в репозитории нет (bin/ в .gitignore),
; настоящий icon.ico лежит в aeros\src — иначе setup.exe собирался без иконки.
SetupIconFile=..\aeros\src\icon.ico
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
VersionInfoCopyright=Copyright (C) 2026 GoGonam AoS.
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Бинарь СТРОГО своей архитектуры и без fallback на общий bin\main.exe:
; если файла нет, ISCC упадёт при компиляции, а не положит чужую архитектуру
; (fallback и давал «CreateProcess сбой; код 216» на ПК пользователя).
Source: "..\aeros\bin\main-x86.exe"; DestDir: "{app}"; DestName: "{#AppExeName}"; Flags: ignoreversion
; DLL — только x86: glfw3.dll из bin является 64-битным и сюда попадать не должен
Source: "..\aeros\bin\glfw3-x86.dll"; DestDir: "{app}"; DestName: "glfw3.dll"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\aeros\bin\*.stl"; DestDir: "{app}\models"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\aeros\2.stl"; DestDir: "{app}\models"; DestName: "2.stl"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\aeros\bin\run.bat"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
; v1.25.0 — отладочный отчёт для разработчика (то же, что F10 в программе)
Source: "debug-report.bat"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\README.md"; DestDir: "{app}"; DestName: "README.txt"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\VERSION"; DestDir: "{app}"; DestName: "VERSION.txt"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\Отладочный отчёт (Debug Report)"; Filename: "{app}\debug-report.bat"; WorkingDir: "{app}"; Comment: "Сохранить Aeros-Debug-Report и показать путь к файлу"
Name: "{group}\{#AppName} {#ArchTitle}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName} {#ArchTitle}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon; WorkingDir: "{app}"

[Run]
; Автозапуск — только если установленный exe совпал по архитектуре (иначе код 216)
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent; Check: AerosLaunchAllowed

[Code]
#include "archcheck.inc"

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    // Контроль архитектуры установленного exe (защита от CreateProcess код 216)
    AerosArchOK := AerosVerifyInstalledExe('{#AppExeName}');
    SaveStringToFile(ExpandConstant('{app}\install-info.txt'),
      'Aeros Engine ' + ExpandConstant('{#AppVersion}') + ' {#ArchTitle}' + #13#10 +
      'Installed: ' + GetDateTimeString('yyyy-mm-dd hh:nn:ss', '-', ':') + #13#10 +
      'Arch: {#ArchSuffix}' + #13#10,
      False);
  end;
end;
