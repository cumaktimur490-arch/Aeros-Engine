; Aeros Engine — Апдейт установщик x86 (32-bit)
; Сборка: iscc AerosEngine-Updater-x86.iss /DAppVersion=1.8.0

#ifndef AppVersion
  #define AppVersion "1.8.0"
#endif
#define Arch "x86"
; Имя файла апдейтера задаётся ДО common.iss (см. там OutputKind)
#define OutputKind "Update"
#include "common.iss"


[Setup]
AppId={{#AppId}
AppName={#AppName} Update x86
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
; Папка по умолчанию вычисляется кодом: берётся путь уже установленной программы
DefaultDirName={code:GetDefaultDirName}
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
ArchitecturesAllowed=x86compatible x64compatible
MinVersion=10.0
PrivilegesRequired=lowest
CloseApplications=yes
Uninstallable=no
CreateUninstallRegKey=no
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} Updater {#ArchTitle} v{#AppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Files]
; Бинарь СТРОГО своей архитектуры и без fallback на общий bin\main.exe:
; если файла нет, ISCC упадёт при компиляции, а не положит чужую архитектуру
; (fallback и давал «CreateProcess сбой; код 216» на ПК пользователя).
Source: "..\aeros\bin\main-x86.exe"; DestDir: "{app}"; DestName: "{#AppExeName}"; Flags: ignoreversion
; DLL — только x86: glfw3.dll из bin является 64-битным и сюда попадать не должен
Source: "..\aeros\bin\glfw3-x86.dll"; DestDir: "{app}"; DestName: "glfw3.dll"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\VERSION"; DestDir: "{app}"; DestName: "VERSION.txt"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\aeros\bin\run.bat"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
; v1.25.0 — отладочный отчёт для разработчика (то же, что F10 в программе)
Source: "debug-report.bat"; DestDir: "{app}"; Flags: ignoreversion

[Code]
var
  InstallPath: String;

#include "archcheck.inc"

function GetExistingInstallPath(): String;
var
  Path: String;
begin
  if RegQueryStringValue(HKLM, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{#AppId}_is1', 'InstallLocation', Path) then
    Result := Path
  else if RegQueryStringValue(HKCU, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{#AppId}_is1', 'InstallLocation', Path) then
    Result := Path
  else
    Result := '';
  if Result = '' then
  begin
    if RegQueryStringValue(HKLM, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{#AppId}_is1', 'Inno Setup: App Path', Path) then
      Result := Path
    else if RegQueryStringValue(HKCU, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{#AppId}_is1', 'Inno Setup: App Path', Path) then
      Result := Path;
  end;
end;

{ Куда ставить обновление: сначала ищем существующую установку по AppId в реестре.
  Раньше найденный путь подставлялся только в DirEdit на странице выбора папки, а
  DefaultDirName оставался «по умолчанию» — в тихом режиме (и если страница папки
  пропущена) обновление уезжало не в ту папку, где реально стоит программа. }
function GetDefaultDirName(Param: String): String;
begin
  if InstallPath = '' then
    InstallPath := GetExistingInstallPath();
  if InstallPath <> '' then
    Result := InstallPath
  else
    Result := ExpandConstant('{autopf}\{#AppName} x86');
end;

function InitializeSetup(): Boolean;
var
  ExistingPath: String;
begin
  Result := True;
  ExistingPath := GetExistingInstallPath();
  if ExistingPath = '' then
  begin
    ExistingPath := ExpandConstant('{autopf}\{#AppName} x86');
    if not FileExists(ExistingPath + '\{#AppExeName}') then
    begin
      if MsgBox('Aeros Engine x86 не найден. Требуется полная установка.' + #13#10 + 'Продолжить в папку по умолчанию?', mbConfirmation, MB_YESNO) = IDNO then
        Result := False;
    end;
  end;
  InstallPath := ExistingPath;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpSelectDir then
    if InstallPath <> '' then
      WizardForm.DirEdit.Text := InstallPath;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    AerosArchOK := AerosVerifyInstalledExe('{#AppExeName}');
    SaveStringToFile(ExpandConstant('{app}\update-info.txt'),
      'Updated to {#AppVersion} {#ArchTitle} ' + GetDateTimeString('yyyy-mm-dd hh:nn:ss', '-', ':') + #13#10, False);
  end;
end;
