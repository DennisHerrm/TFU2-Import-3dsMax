@echo off
REM ================================================================
REM  INSTALLIERE.bat - Paket nach %APPDATA%\Autodesk\ApplicationPlugins
REM  (nur fuer den angemeldeten Benutzer, keine Adminrechte noetig).
REM  Die PackageContents.xml muss DIREKT im Paketordner liegen.
REM ================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"
set "OUTPUT=%~dp0output"
set "DEST=%APPDATA%\Autodesk\ApplicationPlugins\TFU2Import"

if not exist "%OUTPUT%" (echo FEHLER: output\ fehlt - erst BUILD.bat. & goto ende)

REM Laeuft Max noch, ist die alte .dlu gesperrt.
if exist "%DEST%\Contents" for /R "%DEST%\Contents" %%f in (TFU2Import.dl*) do (
  2>nul (>>"%%f" (call )) || (echo FEHLER: %%f ist gesperrt - 3ds Max schliessen. & goto ende)
)

if exist "%DEST%\Contents" rmdir /s /q "%DEST%\Contents"
mkdir "%DEST%\Contents\MacroScripts" >nul 2>&1
mkdir "%DEST%\Contents\Pre-Start-Up_Scripts" >nul 2>&1
mkdir "%DEST%\Contents\Post-Start-Up_Scripts" >nul 2>&1
set ZAHL=0
for /D %%V in ("%OUTPUT%\*") do (
  if exist "%%V\TFU2Import.dlu" (
    mkdir "%DEST%\Contents\%%~nxV" >nul 2>&1
    copy /Y "%%V\TFU2Import.dlu" "%DEST%\Contents\%%~nxV\" >nul
    set /a ZAHL+=1
  )
)
copy /Y "%~dp0package\TFU2Import\PackageContents.xml" "%DEST%\" >nul
copy /Y "%~dp0scripts\TFU2Import.mcr" "%DEST%\Contents\MacroScripts\" >nul
copy /Y "%~dp0scripts\TFU2Menu_2025_2027.ms" "%DEST%\Contents\Pre-Start-Up_Scripts\" >nul
copy /Y "%~dp0scripts\TFU2Menu_2016_2024.ms" "%DEST%\Contents\Post-Start-Up_Scripts\" >nul
echo       %ZAHL% Max-Fassung(en) installiert nach:
echo       %DEST%
echo.
echo In 3ds Max (neu starten): Menue "TFU2 Tool" -^> "Import TFU2"
echo oder Datei -^> Importieren -^> pak0.lp bzw. SWTFU2.exe des Spiels.
:ende
if not "%TFU2_KEIN_PAUSE%"=="1" pause
