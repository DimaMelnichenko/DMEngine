# Запуск DMEngine для проверки изменений:
#   .\Tools\run.ps1 [-Config Debug|Release] [-Seconds 8] [-Screenshot кадр.png] [-Keys 2,3] [-Camera x,y,z,pitch,yaw] [-Level имя]
#                    [-Resolution 1920x1080|desktop] [-NoGui] [-NoMouse] [-NoWind] [-NoParticles]
# Запускает cmake-build-cli-<конфигурация>\DMEngine.exe из корня проекта (все пути движка относительные) с удалённым
# управлением (-remote, канал \\.\pipe\DMEngine, docs/remote.md), ждёт конца инициализации, затем Seconds секунд работы,
# при необходимости нажимает клавиши и снимает кадр, закрывает движок штатно (команда quit: DMGraphics::beforeExit,
# лог дописан до конца) и печатает из log.txt ошибки, время инициализации и строки «GPU average». Принудительно процесс
# закрывается, только если не вышел сам за 30 с.
# Keys — скан-коды клавиш (DIK_*, как в DirectInput): 2 — клавиша «1», 4 — «3», 5 — «4», 16 — Q, 23 — I; нажимает их
# команда key (окну не нужен передний план).
# Screenshot — задний буфер (команда screenshot): ровно то, что рисует движок, без рамки окна, с окнами ImGui, если нет
# -NoGui; у Debug с GPU-based validation тоже (захват экрана давал там чёрный кадр).
# Camera — стартовая камера вместо секции [Camera] в settings.ini: положение и поворот в градусах (тангаж, рыскание).
# Level — уровень из таблицы Levels вместо секции [Level] в settings.ini.
# Resolution — окно и задний буфер (параметр движка -resolution): по умолчанию 1920x1080 — окно с заголовком, снимки
# и замеры сравнимы между запусками и машинами; desktop — окно без рамки на весь монитор, как у движка без параметра.
# NoGui — без окон ImGui (параметр движка -nogui), чтобы они не закрывали кадр; в движке их прячет и клавиша G.
# NoMouse — камера не следует за мышью, указатель скрыт (параметр движка -nomouse); со -Screenshot включается сам,
# чтобы случайное движение мыши не сдвигало кадр.
# NoWind — без ветра (параметр движка -nowind): трава неподвижна, снимки с одной точки совпадают до пикселя.
# NoParticles — без частиц (параметр движка -noparticles): они движутся, и снимки с одной точки иначе не совпадут.
param(
    [ValidateSet('Debug', 'Release')] [string]$Config = 'Debug',
    [int]$Seconds = 8,
    [string]$Screenshot,
    [string]$Keys,
    [string]$Camera,
    [string]$Level,
    [string]$Resolution = '1920x1080',
    [switch]$NoGui,
    [switch]$NoMouse,
    [switch]$NoWind,
    [switch]$NoParticles
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
# Путь снимка — от текущей папки PowerShell (у процесса она может быть другой)
$screenshotPath = if ($Screenshot) { $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Screenshot) }

# Канал удалённого управления: одно подключение за раз, команда — строка, ответ — строки до ok или error: ...
function Connect-Engine([int]$timeoutMs) {
    $pipe = New-Object System.IO.Pipes.NamedPipeClientStream('.', 'DMEngine', [System.IO.Pipes.PipeDirection]::InOut)
    try { $pipe.Connect($timeoutMs) }
    catch { $pipe.Dispose(); return $null }
    return $pipe
}
$probe = Connect-Engine 0
if ($probe) {
    $probe.Dispose()
    throw 'An engine with -remote is already running (Tools\engine.py stop closes it)'
}
function Send-Command([string]$line) {
    $bytes = [Text.Encoding]::UTF8.GetBytes($line + "`n")
    $script:pipe.Write($bytes, 0, $bytes.Length)
    $script:pipe.Flush()
    while ($true) {
        $reply = $script:reader.ReadLine()
        if ($null -eq $reply) { throw "The engine closed the connection after: $line" }
        if ($reply -eq 'ok') { return $true }
        if ($reply.StartsWith('error: ')) {
            Write-Output "$line -> $reply"
            return $false
        }
    }
}

if ($Resolution -notmatch '^(desktop|\d+[xX]\d+)$') { throw "-Resolution: expected WxH or desktop" }
$engineArgs = @('-remote', "-resolution $Resolution")
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
if ($NoParticles) { $engineArgs += '-noparticles' }
$process = Start-Process -FilePath $exe -WorkingDirectory $root -PassThru -ArgumentList ($engineArgs -join ' ')
$begin = Get-Date

# Конец инициализации — канал открылся (Debug с GPU-based validation грузится до минуты)
$pipe = $null
while (-not $pipe -and -not $process.HasExited -and ((Get-Date) - $begin).TotalSeconds -lt 300) {
    $pipe = Connect-Engine 200
}
if (-not $pipe) {
    if ($process.HasExited) { Write-Output "DMEngine exited during initialization, exit code $($process.ExitCode)" }
    else {
        Write-Output 'DMEngine did not finish initialization in 300 s, killed'
        Stop-Process -Id $process.Id -Force
    }
}
else {
    $initSeconds = ((Get-Date) - $begin).TotalSeconds
    $reader = New-Object System.IO.StreamReader($pipe, [Text.Encoding]::UTF8)
    try {
        Start-Sleep -Seconds $Seconds
        foreach ($key in $scanCodes) {
            Send-Command ('key 0x{0:X2}' -f $key) | Out-Null
        }
        # Кадр после нажатий — с их результатом
        if ($scanCodes.Count -gt 0) { Send-Command 'wait 5' | Out-Null }
        if ($Screenshot) {
            $gui = if ($NoGui) { '' } else { ' gui' }
            if (Send-Command "screenshot `"$screenshotPath`"$gui") { Write-Output "Screenshot: $screenshotPath" }
        }
        Send-Command 'quit' | Out-Null
    }
    finally {
        $reader.Dispose()
        $pipe.Dispose()
    }
    if ($process.WaitForExit(30000)) {
        Write-Output ([string]::Format([Globalization.CultureInfo]::InvariantCulture,
                      'DMEngine initialized in {0:F1} s, ran for {1} s, exited normally', $initSeconds, $Seconds))
    }
    else {
        Write-Output 'DMEngine did not exit in 30 s after quit, killed (log may be incomplete)'
        Stop-Process -Id $process.Id -Force
    }
}

$log = Get-Content (Join-Path $root 'log.txt') -Encoding UTF8
$placeholders = @($log | Where-Object { $_ -match 'placeholder( [a-z]+)? is used' }).Count
if ($placeholders -gt 0) { Write-Output "Missing resources replaced by placeholders: $placeholders" }
$log | Where-Object { $_ -match 'error|fail|exception|unknown|Total init|GPU average' -and $_ -notmatch 'placeholder( [a-z]+)? is used' }
