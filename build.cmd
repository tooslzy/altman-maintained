@echo off
setlocal EnableDelayedExpansion

cd /d "%~dp0"

if exist .\build (
	set "CLEAN="
	set /p "CLEAN=Do a clean build (delete .\build)? [y/N] "

	if /i "%CLEAN%"=="y" (
		del /f /s /q .\vcpkg_installed >nul 2>nul
		rmdir /s /q .\vcpkg_installed >nul 2>nul

		del /f /s /q .\build >nul 2>nul
		rmdir /s /q .\build >nul 2>nul
	)
)

mkdir build >nul 2>nul

if not exist %USERPROFILE%\vcpkg\vcpkg.exe (
	echo %USERPROFILE%\vcpkg\vcpkg.exe not found.
	pause >nul 2>nul
	exit /b 1
)

%USERPROFILE%\vcpkg\vcpkg.exe install

cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=%USERPROFILE%\vcpkg\scripts\buildsystems\vcpkg.cmake -A x64 -DCMAKE_BUILD_TYPE=Release > .\build-1.log 2>&1
cmake --build build --config Release -- /m > .\build-2.log 2>&1

pause >nul 2>nul
exit /b 0
