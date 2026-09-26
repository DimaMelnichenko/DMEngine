@echo off
rem Сборка DMEngine из командной строки, без CLion и без ручной настройки окружения.
rem   Tools\build.cmd [debug|release]
rem Находит Visual Studio (любую, с компонентом C++ x64) через vswhere, вызывает vcvars64, при первом запуске
rem конфигурирует CMake (Ninja) в cmake-build-cli-<конфигурация> и собирает. У CLion свои папки сборки, поэтому
rem скрипт и перезагрузка CMake в CLion не мешают друг другу.
rem Из PowerShell: .\Tools\build.cmd debug    Из Git Bash: cmd //c "Tools\build.cmd debug"
setlocal EnableExtensions

set "CONFIG=Debug"
set "CONFIG_DIR=debug"
if "%~1"=="" goto args_done
if /i "%~1"=="debug" goto args_done
if /i "%~1"=="release" (
    set "CONFIG=Release"
    set "CONFIG_DIR=release"
    goto args_done
)
echo Usage: Tools\build.cmd [debug^|release]
exit /b 2
:args_done

rem Вывод компилятора в UTF-8: в кодовой странице консоли (866) русские сообщения MSVC не читаются в терминалах
rem с UTF-8 и в логах. Прежняя кодовая страница восстанавливается в конце, и при ошибке тоже
for /f "tokens=2 delims=:" %%c in ('chcp') do set "OLD_CP=%%c"
set "OLD_CP=%OLD_CP: =%"
set "OLD_CP=%OLD_CP:.=%"
chcp 65001 >nul
call :build
set "RESULT=%errorlevel%"
chcp %OLD_CP% >nul
exit /b %RESULT%

:build
rem С NoDefaultCurrentDirectoryInExePath cmd не ищет программы в текущем каталоге, и шаг CompileShaders.cmd
rem у DirectXTex падает без сообщения. В CLion переменной нет, в терминалах агентов бывает
set "NoDefaultCurrentDirectoryInExePath="

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found: install Visual Studio or Build Tools with the C++ workload
    exit /b 1
)
set "VSDIR="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo No Visual Studio with the C++ x64 toolset found
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
rem Путь к VS содержит "(x86)": внутри блоков в скобках его можно выводить только в кавычках
if not defined VCToolsInstallDir (
    echo vcvars64.bat failed: "%VSDIR%"
    exit /b 1
)

for %%i in ("%~dp0..") do set "ROOT=%%~fi"
set "BUILD_DIR=%ROOT%\cmake-build-cli-%CONFIG_DIR%"

rem Зависимости от заголовков ninja берёт из строк /showIncludes, узнавая их по префиксу, который CMake записывает
rem в кодовой странице консоли на шаге configure. Если папку конфигурировали не в UTF-8, префикс не совпадает
rem с выводом cl, и правка заголовка не пересобирает зависимые .cpp. Поэтому папку без метки configured-utf8
rem конфигурируем заново и собираем с нуля: у уже собранных объектов зависимости не записаны
set "MARKER=%BUILD_DIR%\configured-utf8"
if exist "%MARKER%" goto compile
cmake -S "%ROOT%" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG%
if errorlevel 1 exit /b 1
cmake --build "%BUILD_DIR%" --target clean >nul
type nul > "%MARKER%"

:compile
cmake --build "%BUILD_DIR%"
if errorlevel 1 exit /b 1

echo Built %BUILD_DIR%\DMEngine.exe
exit /b 0
