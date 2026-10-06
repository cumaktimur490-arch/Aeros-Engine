; Aeros Engine — Полный установщик x64 v1.19.0 Vulkan + OpenGL + авто-загрузка зависимостей
; Требует Inno Setup 6.x (https://jrsoftware.org/isinfo.php)
; Сборка: iscc AerosEngine-x64.iss /DAppVersion=1.19.0

#ifndef AppVersion
  #define AppVersion "1.19.0"
#endif
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
#elif FileExists("..\aeros\icon.ico")
SetupIconFile=..\aeros\icon.ico
#endif
UninstallDisplayIcon={app}\{#AppExeName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
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
VersionInfoDescription={#AppName} Setup {#ArchTitle} Vulkan+OpenGL
VersionInfoCopyright=Copyright (C) 2026 GoGonam AoS.
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "quicklaunchicon"; Description: "{cm:CreateQuickLaunchIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked; OnlyBelowVersion: 6.1
Name: "install_deps"; Description: "Download all required dependencies (GLFW, GLM, GLAD, ImGui, Vulkan check)"; GroupDescription: "Dependencies"; Flags: checkedonce
Name: "install_vulkan"; Description: "Check and install Vulkan Runtime (if not present)"; GroupDescription: "Dependencies"; Flags: checkedonce

[Files]
; Основной бинарь — берём arch-specific если есть, иначе fallback main.exe
Source: "..\aeros\bin\main-x64.exe"; DestDir: "{app}"; DestName: "{#AppExeName}"; Flags: ignoreversion; Check: FileExists(ExpandConstant('..\aeros\bin\main-x64.exe'))
Source: "..\aeros\bin\main.exe"; DestDir: "{app}"; DestName: "{#AppExeName}"; Flags: ignoreversion; Check: not FileExists(ExpandConstant('..\aeros\bin\main-x64.exe'))
Source: "..\aeros\bin\aeros-engine-x64.exe"; DestDir: "{app}"; DestName: "aeros-engine-x64.exe"; Flags: ignoreversion skipifsourcedoesntexist
; Vulkan renderer DLLs
Source: "..\aeros\bin\vulkan-1.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
; DLL — все что есть в bin
Source: "..\aeros\bin\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
; Модели STL — примеры
Source: "..\aeros\bin\*.stl"; DestDir: "{app}\models"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\aeros\2.stl"; DestDir: "{app}\models"; DestName: "2.stl"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\models\*.stl"; DestDir: "{app}\models"; Flags: ignoreversion skipifsourcedoesntexist
; Config
Source: "..\aeros\bin\*.ini"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\aeros\bin\*.cfg"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
; Scripts
Source: "..\aeros\build.bat"; DestDir: "{app}\scripts"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\aeros\build-cpu.bat"; DestDir: "{app}\scripts"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\aeros\build-linux.sh"; DestDir: "{app}\scripts"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\aeros\Makefile.linux"; DestDir: "{app}\scripts"; Flags: ignoreversion skipifsourcedoesntexist
; Dependency downloader scripts
Source: "download_deps.ps1"; DestDir: "{app}\scripts"; Flags: ignoreversion
Source: "download_deps_linux.sh"; DestDir: "{app}\scripts"; Flags: ignoreversion
; Лицензия и ридми
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\README.md"; DestDir: "{app}"; DestName: "README.txt"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\RELEASE_NOTES_v1.19.0.md"; DestDir: "{app}"; DestName: "RELEASE_NOTES.txt"; Flags: ignoreversion skipifsourcedoesntexist
; Версия
Source: "..\VERSION"; DestDir: "{app}"; DestName: "VERSION.txt"; Flags: ignoreversion skipifsourcedoesntexist
; Libs — для разработки
Source: "..\libs\glfw\lib-vc2022\glfw3.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
; Docs
Source: "README.md"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\{#AppExeName}"; Comment: "{#AppName} {#ArchTitle} Vulkan+OpenGL"
Name: "{group}\{#AppName} (Vulkan)"; Filename: "{app}\{#AppExeName}"; Parameters: "--vulkan"; WorkingDir: "{app}"; IconFilename: "{app}\{#AppExeName}"; Comment: "{#AppName} Vulkan"
Name: "{group}\{#AppName} (OpenGL)"; Filename: "{app}\{#AppExeName}"; Parameters: "--opengl"; WorkingDir: "{app}"; IconFilename: "{app}\{#AppExeName}"; Comment: "{#AppName} OpenGL"
Name: "{group}\Download Dependencies"; Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -File ""{app}\scripts\download_deps.ps1"" -InstallDir ""{app}"" -Arch x64"; WorkingDir: "{app}"; Comment: "Download all required dependencies"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon; WorkingDir: "{app}"; IconFilename: "{app}\{#AppExeName}"
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: quicklaunchicon; WorkingDir: "{app}"

[Run]
Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -File ""{app}\scripts\download_deps.ps1"" -InstallDir ""{app}"" -Arch x64 -Quiet"; Description: "Download all required dependencies (GLFW, GLM, GLAD, ImGui, models)"; Flags: postinstall runhidden; Tasks: install_deps
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}\logs"
Type: files; Name: "{app}\imgui.ini"
Type: files; Name: "{app}\aeros_settings.ini"

[Code]
function IsVulkanAvailable(): Boolean;
var
  VulkanDll: String;
begin
  VulkanDll := ExpandConstant('{sys}\vulkan-1.dll');
  if FileExists(VulkanDll) then
    Result := True
  else
  begin
    VulkanDll := ExpandConstant('{app}\vulkan-1.dll');
    Result := FileExists(VulkanDll);
  end;
end;

function InitializeSetup(): Boolean;
begin
  if not IsWin64 then
  begin
    MsgBox('Aeros Engine x64 требует 64-битную Windows 10 или новее.' + #13#10 + 'Для 32-битной системы используйте Aeros-Engine-Setup-x86.', mbError, MB_OK);
    Result := False;
  end else
    Result := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  InfoFile: String;
begin
  if CurStep = ssPostInstall then
  begin
    InfoFile := ExpandConstant('{app}\install-info.txt');
    SaveStringToFile(InfoFile,
      'Aeros Engine ' + ExpandConstant('{#AppVersion}') + ' {#ArchTitle} Vulkan+OpenGL' + #13#10 +
      'Installed: ' + GetDateTimeString('yyyy-mm-dd hh:nn:ss', '-', ':') + #13#10 +
      'Arch: {#ArchSuffix}' + #13#10 +
      'Path: ' + ExpandConstant('{app}') + #13#10 +
      'Vulkan: ' + BoolToStr(IsVulkanAvailable(), True) + #13#10 +
      'Renderer: Auto (Vulkan if available, else OpenGL)' + #13#10 +
      'Features: Schlieren, Volumetric Smoke, Shock Waves, Vortex Tubes, Flight 6DOF, LIC, Aeroacoustics' + #13#10 +
      'Linux: Supported via build-linux.sh and Makefile.linux' + #13#10,
      False);

    // Check Vulkan and warn if not available
    if not IsVulkanAvailable() then
    begin
      MsgBox('Vulkan Runtime не найден.' + #13#10 + #13#10 +
             'Aeros Engine будет использовать OpenGL (полностью рабочий).' + #13#10 +
             'Для Vulkan установите:' + #13#10 +
             '- Vulkan Runtime с https://vulkan.lunarg.com/' + #13#10 +
             '- Или драйвер GPU с поддержкой Vulkan (NVIDIA/AMD/Intel)' + #13#10 + #13#10 +
             'OpenGL версия работает без Vulkan и включает все фичи v1.19.0.', mbInformation, MB_OK);
    end;
  end;
end;

function InitializeUninstall(): Boolean;
begin
  Result := True;
end;
