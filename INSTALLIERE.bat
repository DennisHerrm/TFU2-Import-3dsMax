@echo off
REM ================================================================
REM  INSTALLIERE.bat - Paket nach %ProgramData%\Autodesk\ApplicationPlugins
REM  (fuer alle Benutzer, wie das Setup). Ist der Ordner nicht beschreibbar,
REM  startet sich das Skript einmal mit Adminrechten neu.
REM  Die PackageContents.xml muss DIREKT im Paketordner liegen.
REM
REM  Laeuft ein Max-Jahrgang, ist SEINE .dlu gesperrt. Dann wird nur
REM  dieser Jahrgang uebersprungen (und gemeldet) - alle anderen werden
REM  ersetzt.
REM
REM  Bis 0.3.1 wurde nach %APPDATA% installiert. Diese Kopie wird entfernt,
REM  sonst laedt Max das Plugin zweimal (gleiche Klassen-IDs).
REM ================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"
set "OUTPUT=%~dp0output"
set "DEST=%ProgramData%\Autodesk\ApplicationPlugins\TFU2Import"
set "ALT=%APPDATA%\Autodesk\ApplicationPlugins\TFU2Import"

if not exist "%OUTPUT%" (echo FEHLER: output\ fehlt - erst BUILD.bat. & goto ende)

REM ---- Schreibrecht in ProgramData? Sonst einmal als Admin neu starten ----
mkdir "%ProgramData%\Autodesk\ApplicationPlugins" >nul 2>&1
set "PROBE=%ProgramData%\Autodesk\ApplicationPlugins\.tfu2probe"
set "DARF="
2>nul (>>"%PROBE%" (call )) && set "DARF=1"
del "%PROBE%" >nul 2>&1
if not defined DARF (
  if /i "%~1"=="/admin" (echo FEHLER: auch mit Adminrechten kein Schreibzugriff auf %DEST% & goto ende)
  echo       ProgramData braucht Adminrechte - Windows fragt gleich nach.
  powershell -NoProfile -Command "try { $p = Start-Process -FilePath '%~f0' -ArgumentList '/admin' -Verb RunAs -Wait -PassThru -ErrorAction Stop; exit $p.ExitCode } catch { exit 1 }"
  if errorlevel 1 echo FEHLER: ohne Adminrechte nicht installiert.
  goto ende
)

mkdir "%DEST%\Contents\MacroScripts" >nul 2>&1
mkdir "%DEST%\Contents\Pre-Start-Up_Scripts" >nul 2>&1
mkdir "%DEST%\Contents\Post-Start-Up_Scripts" >nul 2>&1
set ZAHL=0
set GESPERRT=0
for /D %%V in ("%OUTPUT%\*") do (
  if exist "%%V\TFU2Import.dlu" (
    mkdir "%DEST%\Contents\%%~nxV" >nul 2>&1
    copy /Y "%%V\TFU2Import.dlu" "%DEST%\Contents\%%~nxV\" >nul 2>&1
    if errorlevel 1 (
      echo       Max %%~nxV laeuft gerade - diese Fassung NICHT ersetzt ^(Max schliessen, dann erneut^)
      set /a GESPERRT+=1
    ) else (
      set /a ZAHL+=1
    )
  )
)
copy /Y "%~dp0package\TFU2Import\PackageContents.xml" "%DEST%\" >nul
copy /Y "%~dp0scripts\TFU2Import.mcr" "%DEST%\Contents\MacroScripts\" >nul
copy /Y "%~dp0scripts\TFU2Menu_2025_2027.ms" "%DEST%\Contents\Pre-Start-Up_Scripts\" >nul
copy /Y "%~dp0scripts\TFU2Menu_2016_2024.ms" "%DEST%\Contents\Post-Start-Up_Scripts\" >nul
echo       %ZAHL% Max-Fassung(en) installiert, %GESPERRT% gesperrt, nach:
echo       %DEST%

REM ---- alte Installation fuer nur diesen Benutzer entfernen -------------
if exist "%ALT%\PackageContents.xml" (
  rmdir /s /q "%ALT%" >nul 2>&1
  if exist "%ALT%\PackageContents.xml" (
    echo       ACHTUNG: alte Kopie in %ALT% ist gesperrt ^(Max laeuft^) - Max schliessen, dann erneut.
  ) else if exist "%ALT%" (
    echo       ACHTUNG: alte Kopie in %ALT% nur teilweise entfernt ^(Max laeuft^) - Max schliessen, dann erneut.
  ) else (
    echo       alte Kopie unter %%APPDATA%% entfernt.
  )
)
echo.
echo In 3ds Max (neu starten): Menue "TFU Tool" -^> "Import TFU" / "TFU Animations"
echo oder Datei -^> Importieren -^> SWTFU.exe bzw. SWTFU2.exe des Spiels. Oben im Fenster: TFU 1 / TFU 2.
:ende
if not "%TFU2_KEIN_PAUSE%"=="1" if /i not "%~1"=="/admin" pause
if /i "%~1"=="/admin" pause
