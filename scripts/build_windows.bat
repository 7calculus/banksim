@echo off
REM BankSim - build, test and package on Windows 10/11.
REM Prerequisites: Visual Studio 2022 (Desktop C++), CMake 3.16+, Qt 6 (MSVC 2019/2022 64-bit kit).
REM Usage: scripts\build_windows.bat "C:\Qt\6.7.3\msvc2022_64"
setlocal
if "%~1"=="" (
  echo Usage: %~nx0 "path\to\Qt\6.x.y\msvc2022_64"
  exit /b 1
)
set QT_DIR=%~1
cd /d "%~dp0\.."
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="%QT_DIR%" || exit /b 1
cmake --build build --config Release --parallel || exit /b 1
echo === Running automated tests ===
build\Release\banksim_tests.exe || (echo TESTS FAILED & exit /b 1)
echo === Packaging ===
if exist dist rmdir /s /q dist
mkdir dist\BankSim
copy build\Release\BankSim.exe dist\BankSim\ >nul
"%QT_DIR%\bin\windeployqt.exe" --release --compiler-runtime --no-translations --no-system-d3d-compiler --no-opengl-sw dist\BankSim\BankSim.exe || exit /b 1
copy build\Release\banksim_tests.exe dist\BankSim\ >nul
copy README.md dist\BankSim\ >nul
echo Done. Double-click dist\BankSim\BankSim.exe
