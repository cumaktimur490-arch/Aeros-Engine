; Aeros Engine — Апдейт установщик x64
; Обновляет существующую установку, проверяет наличие установленного приложения
; Сборка: iscc AerosEngine-Updater-x64.iss /DAppVersion=1.0.0

#define AppVersion "1.0.0"
#define Arch "x64"
#include "common.iss"

#define OutputBaseName "Aeros-Engine-Update-" + ArchSuffix + "-v" + AppVersion

[Setup]
AppId={{#AppId}
AppName={#AppName} Update
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
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
Source: "..\aeros\bin\main-x64.exe"; DestDir: "{app}"; DestName: "{#AppExeName}"; Flags: ignoreversion; Check: FileExists(ExpandConstant('..\aeros\bin\main-x64.exe'))
Source: "..\aeros\bin\main.exe"; DestDir: "{app}"; DestName: "{#AppExeName}"; Flags: ignoreversion; Check: not FileExists(ExpandConstant('..\aeros\bin\main-x64.exe'))
Source: "..\aeros\bin\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\VERSION"; DestDir: "{app}"; DestName: "VERSION.txt"; Flags: ignoreversion skipifsourcedoesntexist
; run.bat обновляем
Source: "..\aeros\bin\run.bat"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Code]
var
  InstallPath: String;

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

function InitializeWizard(): Boolean;
begin
  Result := True;
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
    SaveStringToFile(ExpandConstant('{app}\update-info.txt'),
      'Aeros Engine Updated to ' + ExpandConstant('{#AppVersion}') + ' {#ArchTitle}' + #13#10 +
      'Date: ' + GetDateTimeString('yyyy-mm-dd hh:nn:ss', '-', ':') + #13#10,
      False);
  end;
end;
