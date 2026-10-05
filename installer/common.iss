; Aeros Engine — общие определения для Inno Setup
; Подключается через #include

#define AppName "Aeros Engine"
#define AppPublisher "Aeros Team"
#define AppURL "https://github.com/cumaktimur490-arch/Aeros-Engine"
#define AppExeName "main.exe"

#ifndef AppVersion
  #define AppVersion "1.5.0"
#endif

#ifndef Arch
  #define Arch "x64"
#endif

#if Arch == "x64"
  #define AppId "{A7B8C9D0-E1F2-4A5B-8C9D-0E1F2A3B4C5D}"
  #define ArchSuffix "x64"
  #define ArchTitle "x64 (64-bit)"
#elif Arch == "arm64"
  #define AppId "{A7B8C9D0-E1F2-4A5B-8C9D-0E1F2A3B4C5F}"
  #define ArchSuffix "arm64"
  #define ArchTitle "arm64 (ARM64)"
#else
  #define AppId "{A7B8C9D0-E1F2-4A5B-8C9D-0E1F2A3B4C5E}"
  #define ArchSuffix "x86"
  #define ArchTitle "x86 (32-bit)"
#endif

#ifndef OutputBaseName
  #define OutputBaseName "Aeros-Engine-Setup-" + ArchSuffix + "-v" + AppVersion
#endif
