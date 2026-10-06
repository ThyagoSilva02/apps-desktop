@echo off
setlocal
pushd "%~dp0"

where cl.exe >nul 2>nul
if errorlevel 1 (
    echo Abra o "x64 Native Tools Command Prompt for VS" e execute este arquivo novamente.
    popd
    exit /b 1
)
where rc.exe >nul 2>nul
if errorlevel 1 (
    echo O Windows SDK nao foi encontrado. Instale-o junto com as ferramentas C++ do Visual Studio.
    popd
    exit /b 1
)

if not exist "bin" mkdir "bin"
if not exist "work\obj" mkdir "work\obj"

rc.exe /nologo /I. /fo "work\obj\app.res" "app.rc"
if errorlevel 1 goto :failed

cl.exe /nologo /std:c++17 /EHsc /permissive- /utf-8 /W4 /O2 /MT ^
    /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN ^
    /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 /Isrc ^
    /Fo"work\obj\\" /Fd"work\obj\MacroPillControl.pdb" ^
    /Fe"bin\MacroPillControl.exe" ^
    "src\main.cpp" "src\config.cpp" "src\actions.cpp" "src\serial.cpp" ^
    "work\obj\app.res" ^
    /link /SUBSYSTEM:WINDOWS /MANIFEST:NO ^
    comctl32.lib comdlg32.lib shell32.lib ole32.lib uuid.lib gdi32.lib ^
    user32.lib advapi32.lib uxtheme.lib dwmapi.lib
if errorlevel 1 goto :failed

echo.
echo Compilacao concluida: bin\MacroPillControl.exe
popd
exit /b 0

:failed
echo.
echo A compilacao falhou. Consulte a mensagem acima.
popd
exit /b 1
