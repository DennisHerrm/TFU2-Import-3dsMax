@echo off
REM ================================================================
REM  INSTALLIERE.bat - Paket nach %APPDATA%\Autodesk\ApplicationPlugins
REM  (nur fuer den angemeldeten Benutzer, keine Adminrechte noetig).
REM  Die PackageContents.xml muss DIREKT im Paketordner liegen.
REM
REM  Laeuft ein Max-Jahrgang, ist SEINE .dlu gesperrt. Dann wird nur
REM  dieser Jahrgang uebersprungen (und gemeldet) - alle anderen werden
REM  ersetzt. Frueher brach die ganze Installation ab.
REM ================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"
set "OUTPUT=%~dp0output"
set "DEST=%APPDATA%\Autodesk\ApplicationPlugins\TFU2Import"

if not exist "%OUTPUT%" (echo FEHLER: output\ fehlt - erst BUILD.bat. & goto ende)

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
echo.
echo In 3ds Max (neu starten): Menue "TFU Tool" -^> "Import TFU" / "TFU Animations"
echo oder Datei -^> Importieren -^> SWTFU.exe bzw. SWTFU2.exe des Spiels. Oben im Fenster: TFU 1 / TFU 2.
:ende
if not "%TFU2_KEIN_PAUSE%"=="1" pause
