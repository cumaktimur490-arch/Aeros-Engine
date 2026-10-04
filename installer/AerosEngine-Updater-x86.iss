; Aeros Engine — Апдейт установщик x86 (32-bit)
; Сборка: iscc AerosEngine-Updater-x86.iss /DAppVersion=1.0.0

#ifndef AppVersion
  #define AppVersion "1.1.1"
#endif
#define Arch "x86"
#include "common.iss"

#define OutputBaseName "Aeros-Engine-Update-" + ArchSuffix + "-v" + AppVersion

[Setup]
AppId={{#AppId}
AppName={#AppName} Update x86
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
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
Source: "..\aeros\bin\main-x86.exe"; DestDir: "{app}"; DestName: "{#AppExeName}"; Flags: ignoreversion; Check: FileExists(ExpandConstant('..\aeros\bin\main-x86.exe'))
Source: "..\aeros\bin\main.exe"; DestDir: "{app}"; DestName: "{#AppExeName}"; Flags: ignoreversion; Check: not FileExists(ExpandConstant('..\aeros\bin\main-x86.exe'))
Source: "..\aeros\bin\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\VERSION"; DestDir: "{app}"; DestName: "VERSION.txt"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\aeros\bin\run.bat"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Code]
var
  InstallPath: String;

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
    SaveStringToFile(ExpandConstant('{app}\update-info.txt'),
      'Updated to {#AppVersion} {#ArchTitle} ' + GetDateTimeString('yyyy-mm-dd hh:nn:ss', '-', ':') + #13#10, False);
end;
