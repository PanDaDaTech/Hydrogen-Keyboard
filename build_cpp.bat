@echo off

setlocal EnableDelayedExpansion

cd /d "%~dp0"

set "SRC=%CD%"

rem Detect CI environment (GitHub Actions sets CI=true)
set "IS_CI=%CI%"

rem 1. Locate VsDevCmd.bat

set "VSDEVCMD="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do (
        if exist "%%i\Common7\Tools\VsDevCmd.bat" (
            set "VSDEVCMD=%%i\Common7\Tools\VsDevCmd.bat"
        )
    )
)

rem Fallback search
if not defined VSDEVCMD (
    for /d %%p in ("%ProgramFiles%\Microsoft Visual Studio\*", "%ProgramFiles(x86)%\Microsoft Visual Studio\*") do (
        if exist "%%p\2022\Enterprise\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%%p\2022\Enterprise\Common7\Tools\VsDevCmd.bat"
        if exist "%%p\2022\Community\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%%p\2022\Community\Common7\Tools\VsDevCmd.bat"
        if exist "%%p\2019\Enterprise\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%%p\2019\Enterprise\Common7\Tools\VsDevCmd.bat"
        if exist "%%p\2019\Community\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%%p\2019\Community\Common7\Tools\VsDevCmd.bat"
    )
)

if not defined VSDEVCMD (
    echo [Error] Cannot find VsDevCmd.bat. Please ensure Visual Studio is installed.
    if not defined IS_CI pause
    exit /b 1
)
echo [Info] Found Visual Studio env launcher at: %VSDEVCMD%

rem 2. Check & Download Dependencies (YY-Thunks + VC-LTL)
set "NEED_DOWNLOAD="
set "TC_ROOT=%TOOLCHAIN_ROOT%"
if not defined TC_ROOT set "TC_ROOT=M:\C++"
if not exist "%TC_ROOT%\YY-Thunks\build\native\objs\x86\YY_Thunks_for_WinXP.obj" (
    if not exist "%SRC%\deps\YY-Thunks" (
        set "NEED_DOWNLOAD=1"
    )
)
if defined NEED_DOWNLOAD (
    echo [Info] YY-Thunks / VC-LTL not found locally. Downloading from NuGet...
    call :DOWNLOAD_DEPS
)

rem 3. Build x86 Version
call :BUILD_ARCH x86 5.01
if !errorlevel! neq 0 (
    echo [Error] x86 build failed.
    if not defined IS_CI pause
    exit /b 1
)

rem 3. Build x64 Version
call :BUILD_ARCH x64 5.02
if !errorlevel! neq 0 (
    echo [Error] x64 build failed.
    if not defined IS_CI pause
    exit /b 1
)

rem 4. Package Release with 7-Zip
echo.
echo ========================================
echo Packaging Release 7z Archive...
echo ========================================
if not exist "%SRC%\dist\release" mkdir "%SRC%\dist\release"

set "Z7_EXE="
if exist "C:\Program Files\7-Zip\7z.exe" set "Z7_EXE=C:\Program Files\7-Zip\7z.exe"
if not defined Z7_EXE (
    where 7z.exe >nul 2>&1
    if !errorlevel! equ 0 set "Z7_EXE=7z.exe"
)

if defined Z7_EXE (
    if exist "%SRC%\dist\release\HKeyboard_X86_X64.7z" del /f /q "%SRC%\dist\release\HKeyboard_X86_X64.7z"
    "!Z7_EXE!" a -t7z -mx=9 "%SRC%\dist\release\HKeyboard_X86_X64.7z" "%SRC%\dist\x86\HKeyboard_x86.exe" "%SRC%\dist\x64\HKeyboard_x64.exe"
    if exist "%SRC%\dist\release\HKeyboard_X86_X64.7z" (
        echo [Info] Successfully created release package: dist\release\HKeyboard_X86_X64.7z
    ) else (
        echo [Warn] Failed to create release package.
    )
) else (
    echo [Warn] 7z.exe not found. Skipping packaging.
)

echo.
echo ========================================
echo All Done! Outputs are in dist\x86, dist\x64 and dist\release
echo ========================================
if not defined IS_CI pause
exit /b 0

rem ==============================================
rem Download Dependencies (NuGet)
rem ==============================================
:DOWNLOAD_DEPS
echo.
echo ========================================
echo Downloading YY-Thunks ^& VC-LTL from NuGet...
echo ========================================
if not exist "%SRC%\deps" mkdir "%SRC%\deps"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$yyVer='1.2.2'; $ltlVer='5.3.1';" ^
  "Invoke-WebRequest -Uri \"https://api.nuget.org/v3-flatcontainer/yy-thunks/$yyVer/yy-thunks.$yyVer.nupkg\" -OutFile '%SRC%\deps\yy-thunks.zip';" ^
  "Expand-Archive -Path '%SRC%\deps\yy-thunks.zip' -DestinationPath '%SRC%\deps\YY-Thunks' -Force;" ^
  "Remove-Item '%SRC%\deps\yy-thunks.zip';" ^
  "Invoke-WebRequest -Uri \"https://api.nuget.org/v3-flatcontainer/vc-ltl/$ltlVer/vc-ltl.$ltlVer.nupkg\" -OutFile '%SRC%\deps\vc-ltl.zip';" ^
  "Expand-Archive -Path '%SRC%\deps\vc-ltl.zip' -DestinationPath '%SRC%\deps\VC-LTL' -Force;" ^
  "Remove-Item '%SRC%\deps\vc-ltl.zip';" ^
  "Write-Host 'Done.'"

if exist "%SRC%\deps\YY-Thunks" (
    echo [Info] Dependencies downloaded to deps\
) else (
    echo [Warn] Download may have failed. Build will proceed without XP support.
)
exit /b 0

rem ==============================================
rem Build Subroutine
rem ==============================================
:BUILD_ARCH
setlocal EnableDelayedExpansion
set "ARCH=%1"
set "SUBSYS_VER=%2"

echo.
echo ========================================
echo Building %ARCH% Version... [Subsystem: %SUBSYS_VER%]
echo ========================================

rem Init MSVC Env
call "%VSDEVCMD%" -arch=%ARCH% >nul 2>&1

rem Init directories
if not exist "%SRC%\build\%ARCH%" mkdir "%SRC%\build\%ARCH%"
if not exist "%SRC%\dist\%ARCH%" mkdir "%SRC%\dist\%ARCH%"

rem ==============================================
rem Locate YY-Thunks + VC-LTL toolchain
rem Search order: 1) TOOLCHAIN_ROOT env  2) deps/ (NuGet)  3) M:\C++
rem ==============================================
set "TOOLCHAIN_ROOT=%TOOLCHAIN_ROOT%"
if not defined TOOLCHAIN_ROOT set "TOOLCHAIN_ROOT=M:\C++"

rem --- YY-Thunks ---
set YY_THUNK_OBJ=
rem Try TOOLCHAIN_ROOT (original layout)
if exist "%TOOLCHAIN_ROOT%\YY-Thunks\build\native\objs\%ARCH%\YY_Thunks_for_WinXP.obj" (
    set "YY_THUNK_OBJ=%TOOLCHAIN_ROOT%\YY-Thunks\build\native\objs\%ARCH%\YY_Thunks_for_WinXP.obj"
)
rem Try deps/ (NuGet layout: deps\YY-Thunks\build\native\objs\ARCH\)
if not defined YY_THUNK_OBJ (
    if exist "%SRC%\deps\YY-Thunks\build\native\objs\%ARCH%\YY_Thunks_for_WinXP.obj" (
        set "YY_THUNK_OBJ=%SRC%\deps\YY-Thunks\build\native\objs\%ARCH%\YY_Thunks_for_WinXP.obj"
    )
)
rem Fallback: recursive search in deps\YY-Thunks
if not defined YY_THUNK_OBJ (
    if exist "%SRC%\deps\YY-Thunks" (
        for /f "delims=" %%f in ('dir /s /b "%SRC%\deps\YY-Thunks\YY_Thunks_for_WinXP.obj" 2^>nul') do (
            echo %%f | findstr /i "%ARCH%" >nul && set "YY_THUNK_OBJ=%%f"
        )
    )
)
if defined YY_THUNK_OBJ (
    echo [%ARCH%] Using YY-Thunks: !YY_THUNK_OBJ!
) else (
    echo [%ARCH%] YY-Thunks not found, building without XP thunk support.
)

rem --- VC-LTL ---
set "VCLTL_FOUND="
rem Try TOOLCHAIN_ROOT (original layout)
if exist "%TOOLCHAIN_ROOT%\VC-LTL\build\native\TargetPlatform\header" (
    set "VCLTL_BASE=%TOOLCHAIN_ROOT%\VC-LTL\build\native\TargetPlatform"
    set "VCLTL_FOUND=1"
)
rem Try deps/ (NuGet layout)
if not defined VCLTL_FOUND (
    if exist "%SRC%\deps\VC-LTL\build\native\TargetPlatform\header" (
        set "VCLTL_BASE=%SRC%\deps\VC-LTL\build\native\TargetPlatform"
        set "VCLTL_FOUND=1"
    )
)
rem Fallback: search for TargetPlatform\header in deps\VC-LTL
if not defined VCLTL_FOUND (
    if exist "%SRC%\deps\VC-LTL" (
        for /f "delims=" %%d in ('dir /s /b /ad "%SRC%\deps\VC-LTL\header" 2^>nul') do (
            if not defined VCLTL_FOUND (
                set "VCLTL_BASE=%%d\.."
                set "VCLTL_FOUND=1"
            )
        )
    )
)

if defined VCLTL_FOUND (
    if "%ARCH%"=="x86" (
        set "LTL_VER=5.1.2600.0"
        if exist "!VCLTL_BASE!\5.1.2600.0\lib\Win32" (
            set "LTL_LIB_SUB=lib\Win32"
        ) else if exist "!VCLTL_BASE!\5.1.2600.0\lib\x86" (
            set "LTL_LIB_SUB=lib\x86"
        ) else (
            set "LTL_LIB_SUB=lib\!ARCH!"
        )
    ) else (
        set "LTL_VER=5.2.3790.0"
        if exist "!VCLTL_BASE!\5.2.3790.0\lib\x64" (
            set "LTL_LIB_SUB=lib\x64"
        ) else (
            set "LTL_LIB_SUB=lib\!ARCH!"
        )
    )

    set "VCLTL_INC_DIR1=!VCLTL_BASE!\header"
    set "VCLTL_INC_DIR2=!VCLTL_BASE!\!LTL_VER!\header"
    set "VCLTL_LIB_DIR=!VCLTL_BASE!\!LTL_VER!\!LTL_LIB_SUB!"

    set INCLUDE=!VCLTL_INC_DIR1!;!VCLTL_INC_DIR2!;!INCLUDE!
    set LIB=!VCLTL_LIB_DIR!;!LIB!
    echo [%ARCH%] VC-LTL paths injected.
) else (
    echo [%ARCH%] VC-LTL not found, using default CRT.
)

rem Compile resource file
echo [%ARCH%] Compiling resources...
rc /nologo /c65001 /fo "%SRC%\build\%ARCH%\hkeyboard.res" "%SRC%\hkeyboard.rc"
if !errorlevel! neq 0 (
    echo [Error] Resource compilation failed.
    exit /b 1
)

rem Compile hkeyboard.cpp
echo [%ARCH%] Compiling hkeyboard.cpp ...
cl /nologo /utf-8 /O2 /MT /D_HAS_EXCEPTIONS=0 /GR- /d2FH4- /D_WIN32_WINNT=0x0501 /c "%SRC%\hkeyboard.cpp" /Fo"%SRC%\build\%ARCH%\hkeyboard.obj"
if !errorlevel! neq 0 (
    echo [Error] Compilation failed.
    exit /b 1
)

rem Link
echo [%ARCH%] Linking...
if defined YY_THUNK_OBJ (
    cl /nologo /utf-8 /O2 /MT /D_HAS_EXCEPTIONS=0 /GR- /d2FH4- "%SRC%\build\%ARCH%\hkeyboard.obj" "%SRC%\build\%ARCH%\hkeyboard.res" "!YY_THUNK_OBJ!" /link /OUT:"%SRC%\dist\%ARCH%\HKeyboard_%ARCH%.exe" /SUBSYSTEM:WINDOWS,%SUBSYS_VER% /MANIFEST:NO /CETCOMPAT:NO Comctl32.lib Shell32.lib Gdi32.lib User32.lib Advapi32.lib Imm32.lib
) else (
    cl /nologo /utf-8 /O2 /MT /D_HAS_EXCEPTIONS=0 /GR- /d2FH4- "%SRC%\build\%ARCH%\hkeyboard.obj" "%SRC%\build\%ARCH%\hkeyboard.res" /link /OUT:"%SRC%\dist\%ARCH%\HKeyboard_%ARCH%.exe" /SUBSYSTEM:WINDOWS,%SUBSYS_VER% /MANIFEST:NO Comctl32.lib Shell32.lib Gdi32.lib User32.lib Advapi32.lib Imm32.lib
)
if !errorlevel! neq 0 (
    echo [Error] Linking failed.
    exit /b 1
)

rem Compress with UPX (optional)
if exist "%SRC%\dist\%ARCH%\HKeyboard_%ARCH%.exe" (
    if exist "%SRC%\bin\upx.exe" (
        echo [%ARCH%] Compressing final executable with UPX...
        "%SRC%\bin\upx.exe" --best --force "%SRC%\dist\%ARCH%\HKeyboard_%ARCH%.exe"
    )
)

echo [%ARCH%] Build succeeded: dist\%ARCH%\HKeyboard_%ARCH%.exe
endlocal
exit /b 0
