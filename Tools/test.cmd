@echo off
rem Тесты логики на CPU (docs/tests.md): сборка (Tools\build.cmd) и запуск DMEngineTests.exe из каталога сборки.
rem   Tools\test.cmd [debug|release] [параметры doctest, например -tc=WalkMode*]
rem Код выхода — результат: 0 — все тесты прошли. Без параметра — release
rem Из PowerShell: .\Tools\test.cmd    Из Git Bash: cmd //c "Tools\test.cmd release"
setlocal EnableExtensions EnableDelayedExpansion

rem Параметры doctest — остаток командной строки как есть: %2 %3 … cmd делит и по «=», и -tc=имя пришёл бы двумя словами
set "TOOLS=%~dp0"
set "CONFIG_DIR=release"
set "ARGS=%*"
if /i "%~1"=="debug" (
    set "CONFIG_DIR=debug"
    set "ARGS=!ARGS:~5!"
) else if /i "%~1"=="release" (
    set "ARGS=!ARGS:~7!"
)

call "%TOOLS%build.cmd" %CONFIG_DIR%
if errorlevel 1 exit /b 1

rem Рабочая папка — каталог сборки: LOG пишет log.txt туда, а не в корень (лог движка цел). Exe — с «.\»: без него
rem cmd не ищет в текущей папке, если задан NoDefaultCurrentDirectoryInExePath
pushd "%TOOLS%..\cmake-build-cli-%CONFIG_DIR%"
.\DMEngineTests.exe !ARGS!
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
