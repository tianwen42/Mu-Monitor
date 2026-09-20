@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_and_deploy.ps1" %*
set "EXIT_CODE=%ERRORLEVEL%"
if not "%EXIT_CODE%"=="0" (
    echo.
    echo Build or deployment failed with exit code %EXIT_CODE%.
    if "%~1"=="" pause
    exit /b %EXIT_CODE%
)
echo.
echo Build and deployment completed successfully.
if "%~1"=="" pause
exit /b 0