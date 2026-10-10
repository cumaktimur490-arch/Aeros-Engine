; Aeros Engine — Lite установщик x64 v1.20.0 для слабых устройств i3-3xxx / HD 4000 / GT 620M / 4GB RAM / No CUDA
; Требует Inno Setup 6.x
; Сборка: iscc AerosEngine-Lite-x64.iss /DAppVersion=1.20.0

#ifndef AppVersion
  #define AppVersion "1.20.0"
#endif
#define Arch "x64"
#include "common.iss"

#define LiteAppName "Aeros Engine Lite"
#define LiteAppId "{A7B8C9D0-E1F2-4A5B-8C9D-0E1F2A3B4C5D-LITE}"
#define LiteExeName "aeros-engine-lite.exe"
#define LiteOutputBaseName "Aeros-Engine-Setup-Lite-" + ArchSuffix + "-v" + AppVersion

[Setup]
AppId={{#LiteAppId}
AppName={#LiteAppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={autopf}\{#LiteAppName}
DefaultGroupName={#LiteAppName}
AllowNoIcons=yes
LicenseFile=..\LICENSE
OutputDir=..\release
OutputBaseFilename={#LiteOutputBaseName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
#if FileExists("..\aeros\bin\icon.ico")
SetupIconFile=..\aeros\bin\icon.ico
#elif FileExists("..\aeros\icon.ico")
SetupIconFile=..\aeros\icon.ico
#endif
UninstallDisplayIcon={app}\{#LiteExeName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=6.1
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
VersionInfoDescription={#LiteAppName} Setup {#ArchTitle} Lite for weak devices
VersionInfoCopyright=Copyright (C) 2026 GoGonam AoS.
VersionInfoProductName={#LiteAppName}
VersionInfoProductVersion={#AppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "install_deps"; Description: "Download all required dependencies (GLFW, GLM, GLAD, ImGui, models) — Lite minimal"; GroupDescription: "Dependencies"; Flags: checkedonce

[Files]
; Lite бинарь
Source: "..\aeros\bin\main-lite-x64.exe"; DestDir: "{app}"; DestName: "{#LiteExeName}"; Flags: ignoreversion; Check: FileExists(ExpandConstant('..\aeros\bin\main-lite-x64.exe'))
Source: "..\aeros\bin\main-lite.exe"; DestDir: "{app}"; DestName: "{#LiteExeName}"; Flags: ignoreversion; Check: not FileExists(ExpandConstant('..\aeros\bin\main-lite-x64.exe')) and FileExists(ExpandConstant('..\aeros\bin\main-lite.exe'))
Source: "..\aeros\bin\aeros-engine-lite-x64.exe"; DestDir: "{app}"; DestName: "aeros-engine-lite-x64.exe"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\aeros\bin\aeros-engine-lite.exe"; DestDir: "{app}"; DestName: "aeros-engine-lite.exe"; Flags: ignoreversion skipifsourcedoesntexist
; Fallback to full if lite not built
Source: "..\aeros\bin\main-x64.exe"; DestDir: "{app}"; DestName: "{#LiteExeName}"; Flags: ignoreversion; Check: not FileExists(ExpandConstant('..\aeros\bin\main-lite-x64.exe')) and not FileExists(ExpandConstant('..\aeros\bin\main-lite.exe')) and FileExists(ExpandConstant('..\aeros\bin\main-x64.exe'))
; DLL
Source: "..\aeros\bin\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
; Models
Source: "..\aeros\bin\*.stl"; DestDir: "{app}\models"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\models\*.stl"; DestDir: "{app}\models"; Flags: ignoreversion skipifsourcedoesntexist
; Config — Lite defaults
Source: "..\aeros\bin\*.ini"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
; Scripts
Source: "..\aeros\build-lite.bat"; DestDir: "{app}\scripts"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\aeros\build-lite.sh"; DestDir: "{app}\scripts"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\aeros\Makefile.lite"; DestDir: "{app}\scripts"; Flags: ignoreversion skipifsourcedoesntexist
Source: "download_deps.ps1"; DestDir: "{app}\scripts"; Flags: ignoreversion
; Docs
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\README.md"; DestDir: "{app}"; DestName: "README.txt"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\VERSION"; DestDir: "{app}"; DestName: "VERSION.txt"; Flags: ignoreversion skipifsourcedoesntexist
Source: "README.md"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\{#LiteAppName}"; Filename: "{app}\{#LiteExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\{#LiteExeName}"; Comment: "{#LiteAppName} {#ArchTitle} Lite i3-3xxx HD4000"
Name: "{group}\{#LiteAppName} (Lite Low Power)"; Filename: "{app}\{#LiteExeName}"; Parameters: "--lite"; WorkingDir: "{app}"; IconFilename: "{app}\{#LiteExeName}"; Comment: "Low power mode"
Name: "{group}\Download Lite Dependencies"; Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -File ""{app}\scripts\download_deps.ps1"" -InstallDir ""{app}"" -Arch x64"; WorkingDir: "{app}"; Comment: "Download Lite dependencies"
Name: "{group}\{cm:UninstallProgram,{#LiteAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#LiteAppName}"; Filename: "{app}\{#LiteExeName}"; Tasks: desktopicon; WorkingDir: "{app}"; IconFilename: "{app}\{#LiteExeName}"

[Run]
Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -File ""{app}\scripts\download_deps.ps1"" -InstallDir ""{app}"" -Arch x64 -Quiet"; Description: "Download Lite dependencies (GLFW, GLM, GLAD, models)"; Flags: postinstall runhidden; Tasks: install_deps
Filename: "{app}\{#LiteExeName}"; Description: "{cm:LaunchProgram,{#StringChange(LiteAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}\logs"
Type: files; Name: "{app}\imgui.ini"
Type: files; Name: "{app}\aeros_settings.ini"

[Code]
function InitializeSetup(): Boolean;
begin
  Result := True;
  // Lite works on all Windows from 7+
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    SaveStringToFile(ExpandConstant('{app}\install-info.txt'),
      'Aeros Engine Lite ' + ExpandConstant('{#AppVersion}') + ' {#ArchTitle}' + #13#10 +
      'Installed: ' + GetDateTimeString('yyyy-mm-dd hh:nn:ss', '-', ':') + #13#10 +
      'Arch: {#ArchSuffix} Lite' + #13#10 +
      'Path: ' + ExpandConstant('{app}') + #13#10 +
      'Optimized for: i3-3xxx (Ivy Bridge 2C/4T SSE4.2), Intel HD 4000 (16 EUs), GT 620M, AMD APU, 4GB RAM' + #13#10 +
      'Features: SSE2 only, no AVX2, no CUDA, O1, small binary, 30 FPS target, low RAM <512MB' + #13#10 +
      'Particles: 1500 (was 15000), Streamlines: 8x80 (was 24x300), Voxel: 24 (was 48)' + #13#10 +
      'LBM: OFF by default, 32 res, 1 step/frame, OpenGL 3.3, no Vulkan by default' + #13#10 +
      'Window: 1024x600 for 1366x768 laptops, power saving, VSync ON' + #13#10,
      False);
  end;
end;
