# Запуск DMEngine для проверки изменений:
#   .\Tools\run.ps1 [-Config Debug|Release] [-Seconds 8] [-Screenshot кадр.png] [-Keys 2,3] [-Camera x,y,z,pitch,yaw] [-Level имя]
#                    [-NoGui] [-NoMouse] [-NoWind]
# Запускает cmake-build-cli-<конфигурация>\DMEngine.exe из корня проекта (все пути движка относительные),
# ждёт Seconds секунд, при необходимости нажимает клавиши и снимает окно, затем закрывает движок и печатает
# из log.txt ошибки и время инициализации. Keys — скан-коды DirectInput: 2 — клавиша «1», 4 — «3», 5 — «4», 16 — Q, 23 — I.
# Camera — стартовая камера вместо секции [Camera] в settings.ini: положение и поворот в градусах (тангаж, рыскание).
# Level — уровень из таблицы Levels вместо секции [Level] в settings.ini.
# NoGui — без окон ImGui (параметр движка -nogui), чтобы они не закрывали кадр; в движке их прячет и клавиша G.
# NoMouse — камера не следует за мышью, указатель скрыт (параметр движка -nomouse); со -Screenshot включается сам,
# чтобы случайное движение мыши не сдвигало кадр.
# NoWind — без ветра (параметр движка -nowind): трава неподвижна, снимки с одной точки совпадают до пикселя.
param(
    [ValidateSet('Debug', 'Release')] [string]$Config = 'Debug',
    [int]$Seconds = 8,
    [string]$Screenshot,
    [string]$Keys,
    [string]$Camera,
    [string]$Level,
    [switch]$NoGui,
    [switch]$NoMouse,
    [switch]$NoWind
)

$ErrorActionPreference = 'Stop'

# Keys и Camera — строки, а не массивы: при запуске через powershell -File список через запятую приходит одной
# строкой, и [int[]] прочитал бы "5,4" как 54 (запятая — разделитель тысяч). Из PowerShell массив склеивается
# в строку через пробел; разбор ниже понимает оба варианта
function Split-List([string]$list) {
    return @($list -split '[\s,]+' | Where-Object { $_ })
}
$scanCodes = @(Split-List $Keys | ForEach-Object { [byte]::Parse($_) })
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "cmake-build-cli-$($Config.ToLower())\DMEngine.exe"
if (-not (Test-Path $exe)) {
    throw "Not found: $exe. Build it first: Tools\build.cmd $($Config.ToLower())"
}

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class DMEngineWindow {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@
# Координаты окна в физических пикселях, иначе при масштабе экрана не 100% снимок съезжает
[DMEngineWindow]::SetProcessDPIAware() | Out-Null

$startArgs = @{ FilePath = $exe; WorkingDirectory = $root; PassThru = $true }
$engineArgs = @()
if ($Camera) {
    # Разделитель дробной части — точка при любой локали
    $invariant = [Globalization.CultureInfo]::InvariantCulture
    $values = @(Split-List $Camera | ForEach-Object { [double]::Parse($_, $invariant).ToString($invariant) })
    if ($values.Count -ne 3 -and $values.Count -ne 5) { throw "-Camera: expected x,y,z or x,y,z,pitch,yaw" }
    $engineArgs += "-camera $($values -join ',')"
}
if ($Level) {
    if ($Level -match '\s') { throw "-Level: level name must not contain spaces" }
    $engineArgs += "-level $Level"
}
if ($NoGui) { $engineArgs += '-nogui' }
if ($NoMouse -or $Screenshot) { $engineArgs += '-nomouse' }
if ($NoWind) { $engineArgs += '-nowind' }
if ($engineArgs.Count -gt 0) { $startArgs.ArgumentList = $engineArgs -join ' ' }
$process = Start-Process @startArgs
Start-Sleep -Seconds $Seconds

if ($process.HasExited) {
    Write-Output "DMEngine exited early, exit code $($process.ExitCode)"
}
else {
    if ($scanCodes.Count -gt 0 -or $Screenshot) {
        $process.Refresh()
        $window = $process.MainWindowHandle
        # Нажатия DirectInput получает только окно на переднем плане, а фоновому процессу Windows не всегда даёт
        # вывести окно вперёд. Нажатие Alt перед SetForegroundWindow снимает этот запрет (VK_MENU = 0x12)
        for ($attempt = 0; $attempt -lt 5 -and [DMEngineWindow]::GetForegroundWindow() -ne $window; $attempt++) {
            [DMEngineWindow]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
            [DMEngineWindow]::SetForegroundWindow($window) | Out-Null
            [DMEngineWindow]::keybd_event(0x12, 0, 0x2, [UIntPtr]::Zero)
            Start-Sleep -Milliseconds 200
        }
        if ([DMEngineWindow]::GetForegroundWindow() -ne $window) {
            Write-Output "Warning: DMEngine window is not in the foreground, keys may be lost"
        }
        Start-Sleep -Milliseconds 700

        # DirectInput читает скан-коды: KEYEVENTF_SCANCODE = 0x8, KEYEVENTF_KEYUP = 0x2
        foreach ($key in $scanCodes) {
            [DMEngineWindow]::keybd_event(0, $key, 0x8, [UIntPtr]::Zero)
            Start-Sleep -Milliseconds 150
            [DMEngineWindow]::keybd_event(0, $key, 0xA, [UIntPtr]::Zero)
            Start-Sleep -Milliseconds 400
        }
        if ($scanCodes.Count -gt 0) { Start-Sleep -Milliseconds 800 }

        if ($Screenshot) {
            $rect = New-Object DMEngineWindow+RECT
            [DMEngineWindow]::GetWindowRect($window, [ref]$rect) | Out-Null
            $width = $rect.Right - $rect.Left
            $height = $rect.Bottom - $rect.Top
            $bitmap = New-Object System.Drawing.Bitmap $width, $height
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
            $path = [System.IO.Path]::GetFullPath($Screenshot)
            $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
            $graphics.Dispose()
            $bitmap.Dispose()
            Write-Output "Screenshot: $path (${width}x$height)"
        }
    }
    Stop-Process -Id $process.Id -Force
    Write-Output "DMEngine ran for $Seconds s"
}

$log = Get-Content (Join-Path $root 'log.txt') -Encoding UTF8
$placeholders = @($log | Where-Object { $_ -match 'placeholder( [a-z]+)? is used' }).Count
if ($placeholders -gt 0) { Write-Output "Missing resources replaced by placeholders: $placeholders" }
$log | Where-Object { $_ -match 'error|fail|exception|unknown|Total init|GPU average' -and $_ -notmatch 'placeholder( [a-z]+)? is used' }
