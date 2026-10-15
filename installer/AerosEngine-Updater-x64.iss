; Aeros Engine — Апдейт установщик x64
; Обновляет существующую установку, проверяет наличие установленного приложения
; Сборка: iscc AerosEngine-Updater-x64.iss /DAppVersion=1.8.0

#ifndef AppVersion
  #define AppVersion "1.8.0"
#endif
#define Arch "x64"
; Имя файла апдейтера задаётся ДО common.iss (см. там OutputKind)
#define OutputKind "Update"
#include "common.iss"


[Setup]
AppId={{#AppId}
AppName={#AppName} Update
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
; Папка по умолчанию вычисляется кодом: берётся путь уже установленной программы
DefaultDirName={code:GetDefaultDirName}
DefaultGroupName={#AppName}
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
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=lowest
CloseApplications=yes
RestartApplications=no
Uninstallable=no
CreateUninstallRegKey=no
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} Updater {#ArchTitle} v{#AppVersion}
VersionInfoProductName={#AppName} Updater

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Files]
; Только бинарь и DLL — обновление
; Бинарь СТРОГО своей архитектуры и без fallback на общий bin\main.exe:
; если файла нет, ISCC упадёт при компиляции, а не положит чужую архитектуру
; (fallback и давал «CreateProcess сбой; код 216» на ПК пользователя).
Source: "..\aeros\bin\main-x64.exe"; DestDir: "{app}"; DestName: "{#AppExeName}"; Flags: ignoreversion
; DLL — только x64 (общий шаблон *.dll тащил бы чужие архитектуры: glfw3-x86.dll,
; glfw3-arm64.dll и неиспользуемые NVIDIA Streamline sl.*.dll)
Source: "..\aeros\bin\glfw3.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\aeros\bin\cudart64*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\VERSION"; DestDir: "{app}"; DestName: "VERSION.txt"; Flags: ignoreversion skipifsourcedoesntexist
; run.bat обновляем
Source: "..\aeros\bin\run.bat"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Code]
var
  InstallPath: String;

#include "archcheck.inc"

function GetExistingInstallPath(): String;
var
  Path: String;
begin
  // Проверяем реестр для x64 установки
  if RegQueryStringValue(HKLM, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{#AppId}_is1', 'InstallLocation', Path) then
    Result := Path
  else if RegQueryStringValue(HKCU, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{#AppId}_is1', 'InstallLocation', Path) then
    Result := Path
  else if RegQueryStringValue(HKLM64, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{#AppId}_is1', 'InstallLocation', Path) then
    Result := Path
  else
    Result := '';
  // Если InstallLocation пустой, пробуем Inno Setup Dir
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
    Result := ExpandConstant('{autopf}\{#AppName}');
end;

function InitializeSetup(): Boolean;
var
  ExistingPath: String;
  Msg: String;
begin
  Result := True;
  ExistingPath := GetExistingInstallPath();
  if ExistingPath = '' then
  begin
    // Проверяем дефолтный путь
    ExistingPath := ExpandConstant('{autopf}\{#AppName}');
    if not FileExists(ExistingPath + '\{#AppExeName}') then
    begin
      Msg := 'Aeros Engine не найден в системе.' + #13#10 + #13#10 +
             'Обновление требует установленной полной версии.' + #13#10 +
             'Пожалуйста, сначала установите Aeros-Engine-Setup-x64.' + #13#10 + #13#10 +
             'Продолжить обновление в папку по умолчанию?' + #13#10 + ExistingPath;
      if MsgBox(Msg, mbConfirmation, MB_YESNO) = IDNO then
        Result := False;
    end;
  end;
  InstallPath := ExistingPath;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpSelectDir then
  begin
    if InstallPath <> '' then
      WizardForm.DirEdit.Text := InstallPath;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    AerosArchOK := AerosVerifyInstalledExe('{#AppExeName}');
    SaveStringToFile(ExpandConstant('{app}\update-info.txt'),
      'Aeros Engine Updated to ' + ExpandConstant('{#AppVersion}') + ' {#ArchTitle}' + #13#10 +
      'Date: ' + GetDateTimeString('yyyy-mm-dd hh:nn:ss', '-', ':') + #13#10,
      False);
  end;
end;
