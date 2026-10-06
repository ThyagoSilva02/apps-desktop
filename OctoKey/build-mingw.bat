@echo off
setlocal
pushd "%~dp0"
if not "%~1"=="" set "PATH=%~f1;%PATH%"

set "CXX_TOOL="
set "RC_TOOL="
for %%T in (g++.exe x86_64-w64-mingw32-clang++.exe clang++.exe) do (
    if not defined CXX_TOOL for /f "delims=" %%P in ('where %%T 2^>nul') do if not defined CXX_TOOL set "CXX_TOOL=%%P"
)
for %%T in (windres.exe llvm-windres.exe) do (
    if not defined RC_TOOL for /f "delims=" %%P in ('where %%T 2^>nul') do if not defined RC_TOOL set "RC_TOOL=%%P"
)
if not defined CXX_TOOL goto :missing
if not defined RC_TOOL goto :missing
if not exist "bin" mkdir "bin"
if not exist "work\obj" mkdir "work\obj"

"%RC_TOOL%" -I. -i app.rc -O coff -o "work\obj\app.res.o"
if errorlevel 1 goto :failed
"%CXX_TOOL%" -std=c++17 -O2 -Wall -Wextra -Wpedantic -DUNICODE -D_UNICODE ^
    -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 ^
    -Isrc -municode -mwindows -static ^
    src\main.cpp src\config.cpp src\actions.cpp src\serial.cpp "work\obj\app.res.o" ^
    -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -lgdi32 -luser32 -ladvapi32 ^
    -luxtheme -ldwmapi -o "bin\MacroPillControl.exe"
if errorlevel 1 goto :failed
echo Compilacao concluida: bin\MacroPillControl.exe
popd
exit /b 0

:missing
echo Um toolchain MinGW-w64 completo nao foi encontrado.
echo Use: build-mingw.bat "C:\caminho\do\toolchain\bin"
popd
exit /b 1

:failed
echo A compilacao falhou. Consulte a mensagem acima.
popd
exit /b 1
