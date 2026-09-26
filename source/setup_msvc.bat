@echo off
REM Localiza un toolchain MSVC x64 sin depender de una edicion/version fija.
REM No usa setlocal: vcvars64 debe modificar el entorno del build que lo llama.

where cl >nul 2>&1
if not errorlevel 1 exit /b 0

set "FML_VCVARS="
set "FML_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%FML_VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%FML_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "FML_VCVARS=%%I\VC\Auxiliary\Build\vcvars64.bat"
)

if not defined FML_VCVARS if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "FML_VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not defined FML_VCVARS if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "FML_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined FML_VCVARS if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" set "FML_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
if not defined FML_VCVARS (
    echo [ERROR] no encuentro MSVC x64. Instala "Desktop development with C++".
    exit /b 1
)

call "%FML_VCVARS%" >nul 2>&1
where cl >nul 2>&1
if errorlevel 1 (
    echo [ERROR] vcvars64.bat no configuro cl.exe
    exit /b 1
)
exit /b 0
