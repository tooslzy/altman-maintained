@echo off
setlocal EnableDelayedExpansion

cd /d "%~dp0"

if exist .\build (
	set "CLEAN="
	set /p "CLEAN=Do a clean build (delete .\build and .\vcpkg_installed)? [y/N] "

	if /i "!CLEAN!"=="y" (
		echo Cleaning previous build artifacts...

		del /f /s /q .\vcpkg_installed\ >nul 2>nul
		rmdir /s /q .\vcpkg_installed\* >nul 2>nul
		for /D %%D in (".\vcpkg_installed\*") do (
			del /f /s /q "%%D" >nul 2>&1
			rd /s /q "%%D" >nul 2>&1
		)

		del /f /s /q .\build\* >nul 2>nul
		rmdir /s /q .\build\ >nul 2>nul
		for /D %%D in (".\build\*") do (
			del /f /s /q "%%D" >nul 2>&1
			rd /s /q "%%D" >nul 2>&1
		)
	)
)

if exist .\build\altman\ (
	del /f /s /q .\build\altman\* >nul 2>nul
	rmdir /s /q .\build\altman\ >nul 2>nul
	for /D %%D in (".\build\altman\*") do (
		del /f /s /q "%%D" >nul 2>&1
		rd /s /q "%%D" >nul 2>&1
	)
)

if not exist .\build (
	mkdir build >nul 2>nul
)

if not exist %USERPROFILE%\vcpkg\vcpkg.exe (
	echo %USERPROFILE%\vcpkg\vcpkg.exe not found.
	pause >nul 2>&1
	exit /b 1
)

echo Installing dependencies...
"%USERPROFILE%\vcpkg\vcpkg.exe" install --triplet x64-windows-static
if %ERRORLEVEL% neq 0 (
	echo vcpkg install failed.
	exit /b %ERRORLEVEL%
)

echo Configuring CMake...
cmake -B build -S . ^
    -DCMAKE_TOOLCHAIN_FILE="%USERPROFILE%\vcpkg\scripts\buildsystems\vcpkg.cmake" ^
    -A x64 ^
    -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
    -DCMAKE_BUILD_TYPE=Release > .\build-1.log 2>&1

if %ERRORLEVEL% neq 0 (
	echo CMake configuration failed. See build-1.log for details.
	exit /b %ERRORLEVEL%
)

echo Building project...
cmake --build build --config Release --parallel > .\build-2.log 2>&1

if %ERRORLEVEL% neq 0 (
	echo Build failed. See build-2.log for details.
	exit /b %ERRORLEVEL%
)

echo Build successful.
exit /b 0
