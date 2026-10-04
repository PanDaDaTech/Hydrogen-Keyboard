@echo off
rem ============================================================================
rem  release.cmd -- one command to publish a Release (run workflow + fill notes)
rem ----------------------------------------------------------------------------
rem  Usage:
rem    release.cmd                 create a DRAFT release, auto-apply the notes file
rem    release.cmd --publish       same, but publish the release directly
rem    release.cmd --tag v2.0      set the tag (default v2.0; date is auto-appended)
rem    release.cmd --notes FILE    use a specific notes file
rem
rem  Notes file lookup order:
rem    1) --notes FILE argument
rem    2) .workbuddy\release_body_<YYYYMMDD>.md
rem    3) .workbuddy\release_body.md
rem    4) RELEASE_NOTES.md  (repo root, versioned in git)
rem    5) none -> keep GitHub's auto-generated notes
rem
rem  NOTE: keep this file pure ASCII. cmd.exe reads scripts using the system
rem        code page, so UTF-8 CJK characters would be garbled.
rem ============================================================================
setlocal enabledelayedexpansion

cd /d "%~dp0"

set "TAG=v2.0"
set "DRAFT=true"
set "NOTES="

rem ---- parse arguments ----
:parse
if "%~1"=="" goto parsed
if /i "%~1"=="--publish"   ( set "DRAFT=false" & shift & goto parse )
if /i "%~1"=="--draft"     ( set "DRAFT=true"  & shift & goto parse )
if /i "%~1"=="--tag"       ( set "TAG=%~2" & shift & shift & goto parse )
if /i "%~1"=="--notes"     ( set "NOTES=%~2" & shift & shift & goto parse )
echo [error] Unknown option: %~1
echo         Usage: release.cmd [--publish] [--tag v2.0] [--notes path]
exit /b 2
:parsed

rem ---- Beijing date (the workflow resolves the date the same way) ----
for /f %%d in ('powershell -NoProfile -Command "(Get-Date).ToString(''yyyyMMdd'')"') do set "DATESTR=%%d"
if "%DATESTR%"=="" set "DATESTR=00000000"

rem ---- locate the notes file ----
if not defined NOTES (
    if exist ".workbuddy\release_body_%DATESTR%.md" set "NOTES=.workbuddy\release_body_%DATESTR%.md"
)
if not defined NOTES (
    if exist ".workbuddy\release_body.md" set "NOTES=.workbuddy\release_body.md"
)
if not defined NOTES (
    if exist "RELEASE_NOTES.md" set "NOTES=RELEASE_NOTES.md"
)

echo ============================================================
echo  Tag     : %TAG%  (workflow appends the Beijing date)
echo  Draft   : %DRAFT%
if defined NOTES (
    echo  Notes   : %NOTES%
) else (
    echo  Notes   : none - GitHub auto-generated notes will be used
)
echo ============================================================
echo.

rem ---- remember the newest run before triggering ----
for /f "tokens=*" %%i in ('gh run list --workflow=release.yml --limit 1 --json databaseId -q ".[0].databaseId" 2^>nul') do set "OLD_RUN=%%i"

rem ---- trigger the workflow ----
call gh workflow run release.yml -f tag=%TAG% -f title= -f draft=%DRAFT% -f prerelease=false
if errorlevel 1 ( echo [error] Failed to trigger the workflow. & exit /b 1 )

echo.
echo [info] Workflow triggered. Waiting for the new run to appear...
set "RUN_ID="
for /l %%n in (1,1,30) do (
    if not defined RUN_ID (
        timeout /t 3 /nobreak >nul
        for /f "tokens=*" %%i in ('gh run list --workflow=release.yml --limit 1 --json databaseId -q ".[0].databaseId" 2^>nul') do set "NEW_RUN=%%i"
        if not "!NEW_RUN!"=="%OLD_RUN%" set "RUN_ID=!NEW_RUN!"
    )
)

if not defined RUN_ID (
    echo [error] Could not detect the new run. Check: gh run list --workflow=release.yml
    exit /b 1
)

echo [info] Run ID: %RUN_ID%
echo [info] Watching the build (x86 / x64 / arm64)...
call gh run watch %RUN_ID% --exit-status
if errorlevel 1 (
    echo.
    echo [error] The release workflow failed. Logs:
    echo         gh run view %RUN_ID% --log-failed
    exit /b 1
)

rem ---- resolve the real tag (v2.0 -> v2.0_YYYYMMDD) ----
rem The newest release (draft or not) is the one we just created.
set "REAL_TAG="
for /f "tokens=*" %%i in ('gh release list --limit 1 --json tagName -q ".[0].tagName" 2^>nul') do set "REAL_TAG=%%i"
if not defined REAL_TAG set "REAL_TAG=%TAG%"

echo.
echo [info] Build succeeded. Resolved release: %REAL_TAG%

rem ---- apply the notes ----
if defined NOTES (
    echo [info] Applying release notes from %NOTES% ...
    call gh release edit "%REAL_TAG%" --notes-file "%NOTES%"
    if errorlevel 1 (
        echo [warn] Failed to apply the notes file. Apply it manually:
        echo        gh release edit %REAL_TAG% --notes-file %NOTES%
    ) else (
        echo [ok] Notes applied.
    )
)

echo.
if /i "%DRAFT%"=="true" (
    echo [done] Draft release created: %REAL_TAG%
    echo        Preview : gh release view %REAL_TAG%
    echo        Publish : gh release edit %REAL_TAG% --draft=false
    echo        Discard : gh release delete %REAL_TAG% --yes --cleanup-tag
) else (
    echo [done] Release published: %REAL_TAG%
    echo        View    : gh release view %REAL_TAG% --web
)

endlocal
