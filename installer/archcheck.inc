{ ------------------------------------------------------------------
  Aeros Engine - общий код проверки архитектуры установленного exe.

  Подключается ВНУТРИ секции [Code] каждого .iss одноимённой include-директивой
  ISPP (имя файла указано в .iss; здесь его писать НЕЛЬЗЯ - препроцессор
  разбирает директивы раньше, чем комментарии Pascal, и уходит в рекурсию:
  "File ... is already being included").

  Вызов из .iss:
      AerosArchOK := AerosVerifyInstalledExe(имя_exe_из_AppExeName);

  Зачем: ошибка "CreateProcess сбой; код 216 / эта версия %1 не совместима
  с версией Windows" означает, что на диск лёг бинарь чужой архитектуры.
  Раньше выбор бинаря делался через Check: FileExists(...) в секции [Files] -
  но Check выполняется во время установки на ПК пользователя, где
  относительного пути к aeros/bin/main-ARCH.exe не существует, поэтому
  правильный exe пропускался, а ставился общий fallback bin/main.exe
  (архитектуры последней сборки). Теперь выбор бинаря делается на этапе
  компиляции установщика, а этот модуль - дополнительный контроль уже на
  машине пользователя: вместо кода 216 человек видит понятное сообщение.
  ------------------------------------------------------------------ }

var
  AerosArchOK: Boolean;

{ IntToHex в секции [Code] Inno Setup НЕТ (эта функция есть только у
  препроцессора ISPP и во внутренних модулях Setup) - компилятор отвечает
  Unknown identifier. Форматируем шестнадцатеричное значение
  поля Machine сами - только на стандартных Copy, mod и div. }
function AerosHex4(Val: Integer): String;
var
  Digits: String;
  V, I, D: Integer;
begin
  Digits := '0123456789ABCDEF';
  Result := '';
  V := Val and $FFFF;
  for I := 1 to 4 do
  begin
    D := (V mod 16) + 1;
    Result := Copy(Digits, D, 1) + Result;
    V := V div 16;
  end;
end;

{ Ожидаемое значение поля Machine из PE-заголовка для этого установщика:
  $8664 = x64, $014C = x86, $AA64 = arm64. }
function AerosExpectedPEMachine: Integer;
begin
#if Arch == "x64"
  Result := $8664;
#elif Arch == "arm64"
  Result := $AA64;
#else
  Result := $014C;
#endif
end;

{ Человекочитаемое имя архитектуры этого установщика. }
function AerosArchName: String;
begin
#if Arch == "x64"
  Result := 'x64 (64-bit Intel/AMD)';
#elif Arch == "arm64"
  Result := 'arm64 (ARM64)';
#else
  Result := 'x86 (32-bit)';
#endif
end;

{ Читает поле Machine из PE-заголовка файла. 0 = прочитать не удалось.
  e_lfanew лежит по смещению 0x3C (в строке 1-based это байты 61..64),
  за подписью 'PE'+0+0 идёт IMAGE_FILE_HEADER, первое поле — Machine (2 байта). }
function AerosReadPEMachine(const FileName: String): Integer;
var
  Data: AnsiString;
  N, PEOffset, PESig: Integer;
begin
  Result := 0;
  if not FileExists(FileName) then Exit;
  if not LoadStringFromFile(FileName, Data) then Exit;
  N := Length(Data);
  if N < 4096 then Exit;
  if (Ord(Data[1]) <> $4D) or (Ord(Data[2]) <> $5A) then Exit;   { 'MZ' }
  PEOffset := Ord(Data[61]) + Ord(Data[62]) * 256 + Ord(Data[63]) * 65536 + Ord(Data[64]) * 16777216;
  if (PEOffset < 64) or (PEOffset + 8 > N) then Exit;
  PESig := Ord(Data[PEOffset + 1]) + Ord(Data[PEOffset + 2]) * 256 +
           Ord(Data[PEOffset + 3]) * 65536 + Ord(Data[PEOffset + 4]) * 16777216;
  if PESig <> $4550 then Exit;                                  { 'PE' + 0 + 0 }
  Result := Ord(Data[PEOffset + 5]) + Ord(Data[PEOffset + 6]) * 256;
end;

{ Проверяет установленный exe (ExeName — имя файла в папке {app}).
  True = архитектура совпала и файл можно запускать. }
function AerosVerifyInstalledExe(const ExeName: String): Boolean;
var
  ExePath: String;
  Mach, Want: Integer;
  Report: String;
begin
  ExePath := ExpandConstant('{app}') + '\' + ExeName;
  Want := AerosExpectedPEMachine;
  Mach := AerosReadPEMachine(ExePath);
  Result := (Mach <> 0) and (Mach = Want);
  Log(Format('[Aeros] arch check: file=%s machine=%s expected=%s',
    [ExePath, AerosHex4(Mach), AerosHex4(Want)]));

  if not Result then
  begin
    Report :=
      'Aeros Engine: архитектура установленного файла не совпала.' + #13#10 +
      'Файл: ' + ExePath + #13#10 +
      'Ожидалось (PE machine): ' + AerosHex4(Want) + ' — ' + AerosArchName + #13#10 +
      'В файле: ' + AerosHex4(Mach) + #13#10 +
      'Дата: ' + GetDateTimeString('yyyy-mm-dd hh:nn:ss', '-', ':') + #13#10;
    SaveStringToFile(ExpandConstant('{app}') + '\ARCH-MISMATCH.txt', Report, False);

    { В тихом режиме (CI, /VERYSILENT) окно не показываем — иначе установка
      заблокируется; факт несовпадения всё равно виден по ARCH-MISMATCH.txt и логу. }
    if not WizardSilent then
      MsgBox(
        'Aeros Engine: установленный ' + ExeName + ' не соответствует архитектуре этой Windows.' + #13#10 + #13#10 +
        'Установщик: ' + AerosArchName + ' (PE machine ' + AerosHex4(Want) + ')' + #13#10 +
        'В файле оказалось: ' + AerosHex4(Mach) + #13#10 + #13#10 +
        'Запустить такой файл нельзя — Windows выдаёт «CreateProcess сбой; код 216».' + #13#10 +
        'Скачайте установщик под свою архитектуру со страницы релиза:' + #13#10 +
        '  • обычный ПК Intel/AMD, 64-bit Windows — Setup-x64' + #13#10 +
        '  • 32-bit Windows — Setup-x86' + #13#10 +
        '  • ARM (Snapdragon) — Setup-arm64' + #13#10 + #13#10 +
        'Подробности записаны в ARCH-MISMATCH.txt в папке установки.',
        mbCriticalError, MB_OK);
  end;
end;

{ Флаг для параметра Check: в секции [Run] — Inno требует функцию, а не переменную.
  Запускать установленный exe можно только если архитектура совпала. }
function AerosLaunchAllowed: Boolean;
begin
  Result := AerosArchOK;
end;
