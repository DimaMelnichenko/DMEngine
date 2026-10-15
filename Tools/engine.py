"""Удалённое управление запущенным DMEngine — как консоль и Remote Control API в UE (docs/remote.md).

    python Tools/engine.py start [--config Debug|Release] [--camera x,y,z,pitch,yaw] [--level имя] [--resolution 1920x1080|desktop] [--nogui] [--mouse] [--nowind] [--noparticles] [--pix] [--gpu-validation]
    python Tools/engine.py [--timeout с] <команда ...>    одна команда, печатает ответ; код выхода 1 при error
    python Tools/engine.py [--timeout с] run сценарий.txt  команды из файла по строке (# — комментарий), за одно подключение
    python Tools/engine.py stop                   quit, ожидание выхода, ошибки из log.txt

start запускает cmake-build-cli-<config>\\DMEngine.exe из корня проекта с -remote (канал \\\\.\\pipe\\DMEngine),
-nomouse (без --mouse: камера не следует за мышью) и -resolution (по умолчанию 1920x1080 — окно с заголовком, кадры
и замеры сравнимы между машинами; desktop — без рамки на весь монитор) и ждёт конца инициализации и первого кадра
(команда wait 1). Команды движка — help; путь снимка (screenshot файл) считается от текущей папки. Сообщения скрипта —
ASCII (правило Tools/).

Ответ на команду — не дольше --timeout (60 с; у stat gpu N — ещё N, у pix — 300): не ответил — главный цикл движка стоит
(в log.txt — строки «Frame watchdog»: на каком шаге кадра), скрипт печатает их и выходит с кодом 2; start и stop такой
движок закрывают принудительно. --config — сборка, по умолчанию Release; --gpu-validation — GPU-based validation
(-gpuvalidation) для правок слоя D3D12, только в Debug: без --config запускает Debug, с --config Release — ошибка.
"""
import argparse
import ctypes
import os
import re
import subprocess
import sys
import tempfile
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIPE = r'\\.\pipe\DMEngine'
PID_FILE = os.path.join(tempfile.gettempdir(), 'dmengine_remote.pid')
TIMEOUT = 60.0     # ответ на команду, с (--timeout)


class Stuck(Exception):
    """Нет ответа за отведённое время: главный цикл движка стоит"""


def connect(timeout=10.0):
    """Канал движка; ждёт, пока он свободен (одно подключение за раз)"""
    deadline = time.time() + timeout
    while True:
        try:
            return open(PIPE, 'r+b', buffering=0)
        except OSError:
            if time.time() > deadline:
                return None
            time.sleep(0.1)


def command_timeout(line):
    """Сколько ждать ответа: команды, которые сами ждут секунды, — дольше"""
    words = line.split()
    if len(words) >= 3 and words[0] == 'stat':
        try:
            return TIMEOUT + float(words[2])
        except ValueError:
            pass
    if words and words[0] == 'pix':
        return max(TIMEOUT, 300.0)
    return TIMEOUT


def command(pipe, line):
    """Команда и ответ: строки, последняя - ok или error: ...; нет ответа за command_timeout - Stuck. Чтение канала
    блокирующее, поэтому - в потоке: зависший движок держит его до выхода скрипта"""
    result = []

    def exchange():
        try:
            result.append(exchange_lines(pipe, line))
        except BaseException as error:   # и SystemExit: движок закрыл канал
            result.append(error)

    worker = threading.Thread(target=exchange, daemon=True)
    worker.start()
    worker.join(command_timeout(line))
    if not result:
        raise Stuck(line)
    if isinstance(result[0], BaseException):
        raise result[0]
    return result[0]


def exchange_lines(pipe, line):
    pipe.write((line + '\n').encode('utf-8'))
    data = b''
    while True:
        chunk = pipe.read(65536)
        if not chunk:
            raise SystemExit('error: the engine closed the connection')
        data += chunk
        if data.endswith(b'\n'):
            lines = data.decode('utf-8', 'replace').rstrip('\n').split('\n')
            if lines[-1] == 'ok' or lines[-1].startswith('error: '):
                return lines


def quote(word):
    return '"%s"' % word if (' ' in word or not word) else word


def absolute_paths(words):
    """screenshot <файл>: путь движок считает от корня проекта, а человек пишет от текущей папки"""
    if len(words) >= 2 and words[0].lower() == 'screenshot':
        words = [words[0], os.path.abspath(words[1])] + words[2:]
    return words


def split_line(line):
    words, word, quoted, has_word = [], '', False, False
    for c in line:
        if c == '"':
            quoted, has_word = not quoted, True
        elif not quoted and c in ' \t':
            if has_word:
                words.append(word)
            word, has_word = '', False
        else:
            word, has_word = word + c, True
    if has_word:
        words.append(word)
    return words


def send(pipe, words):
    lines = command(pipe, ' '.join(quote(w) for w in absolute_paths(words)))
    for line in lines:
        print(line)
    return not lines[-1].startswith('error')


def abandon_stuck(line, pid=None):
    """Нет ответа: строки сторожа кадра из лога и выход без закрытия канала (его держит поток чтения, а CRT закрыл бы
    канал только после чтения, то есть никогда); с pid — движок закрывается принудительно"""
    print('error: DMEngine did not answer "%s" in %.0f s: its main loop is stuck' % (line, command_timeout(line)))
    try:
        with open(os.path.join(ROOT, 'log.txt'), encoding='utf-8', errors='replace') as f:
            watchdog = [entry for entry in f.read().splitlines() if 'Frame watchdog' in entry]
    except OSError:
        watchdog = []
    for entry in watchdog[-5:]:
        print(entry)
    if not watchdog:
        print('(no Frame watchdog lines in log.txt yet: the frame is stuck for less than its limit)')
    if pid:
        kill(pid)
        print('DMEngine killed (main loop stuck)')
    sys.stdout.flush()
    os._exit(2)


def kill(pid):
    handle = ctypes.windll.kernel32.OpenProcess(0x0001, False, pid)   # PROCESS_TERMINATE
    if handle:
        ctypes.windll.kernel32.TerminateProcess(handle, 1)
        ctypes.windll.kernel32.CloseHandle(handle)


def process_alive(pid):
    handle = ctypes.windll.kernel32.OpenProcess(0x00100000, False, pid)   # SYNCHRONIZE
    if not handle:
        return False
    alive = ctypes.windll.kernel32.WaitForSingleObject(handle, 0) != 0
    ctypes.windll.kernel32.CloseHandle(handle)
    return alive


def wait_exit(pid, timeout):
    handle = ctypes.windll.kernel32.OpenProcess(0x00100000, False, pid)
    if not handle:
        return True
    exited = ctypes.windll.kernel32.WaitForSingleObject(handle, int(timeout * 1000)) == 0
    ctypes.windll.kernel32.CloseHandle(handle)
    return exited


def print_log():
    """Как Tools/run.ps1: число заглушек, ошибки, время инициализации, строки GPU average и CPU average"""
    try:
        with open(os.path.join(ROOT, 'log.txt'), encoding='utf-8', errors='replace') as f:
            log = f.read().splitlines()
    except OSError:
        return
    placeholder = re.compile(r'placeholder( [a-z]+)? is used')
    count = sum(1 for line in log if placeholder.search(line))
    if count:
        print('Missing resources replaced by placeholders: %d' % count)
    report = re.compile(r'error|fail|exception|unknown|Total init|GPU average|CPU average', re.I)
    for line in log:
        if report.search(line) and not placeholder.search(line):
            print(line)


def start(args):
    probe = connect(timeout=0)
    if probe:
        probe.close()
        raise SystemExit('error: an engine with -remote is already running')
    # Слой проверки есть только в Debug: --gpu-validation без --config — Debug, иначе Release без проверки шёл бы молча
    if args.config is None:
        args.config = 'Debug' if args.gpu_validation else 'Release'
    elif args.gpu_validation and args.config == 'Release':
        raise SystemExit('error: --gpu-validation needs the Debug build (--config Debug)')
    exe = os.path.join(ROOT, 'cmake-build-cli-%s' % args.config.lower(), 'DMEngine.exe')
    if not os.path.exists(exe):
        raise SystemExit('error: not found %s, build it first: Tools\\build.cmd %s' % (exe, args.config.lower()))
    engine_args = ['-remote', '-resolution', args.resolution]
    if not args.mouse:
        engine_args.append('-nomouse')
    if args.nogui:
        engine_args.append('-nogui')
    if args.nowind:
        engine_args.append('-nowind')
    if args.noparticles:
        engine_args.append('-noparticles')
    if args.pix:
        engine_args.append('-pix')
    if args.gpu_validation:
        engine_args.append('-gpuvalidation')
    if args.camera:
        engine_args += ['-camera', args.camera]
    if args.level:
        engine_args += ['-level', args.level]
    flags = 0x00000008 | 0x00000200   # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP
    process = subprocess.Popen([exe] + engine_args, cwd=ROOT, creationflags=flags, close_fds=True)
    with open(PID_FILE, 'w') as f:
        f.write(str(process.pid))

    begin = time.time()
    while time.time() - begin < 120:
        if process.poll() is not None:
            print('DMEngine exited during initialization, exit code %d' % process.returncode)
            print_log()
            return 1
        pipe = connect(timeout=0)
        if pipe:
            # Канал открыт в конце инициализации; первый кадр — команда wait 1: главный цикл идёт
            try:
                command(pipe, 'wait 1')
            except Stuck:
                abandon_stuck('wait 1', process.pid)
            pipe.close()
            print('DMEngine started (%s, pid %d) in %.1f s' % (args.config, process.pid, time.time() - begin))
            return 0
        time.sleep(0.2)
    print('error: no pipe after 120 s')
    return 1


def stop():
    pid = None
    try:
        with open(PID_FILE) as f:
            pid = int(f.read())
    except (OSError, ValueError):
        pass
    pipe = connect(timeout=2)
    if pipe:
        try:
            send(pipe, ['quit'])
        except Stuck:
            abandon_stuck('quit', pid)
        pipe.close()
    elif not (pid and process_alive(pid)):
        print('DMEngine is not running')
        print_log()
        return 0
    if pid and not wait_exit(pid, 20):
        kill(pid)
        print('error: DMEngine did not exit in 20 s, killed (log.txt may be incomplete)')
        return 1
    print('DMEngine exited')
    print_log()
    return 0


def run_script(path):
    pipe = connect()
    if not pipe:
        raise SystemExit('error: no engine, start it: python Tools/engine.py start')
    ok = True
    with open(path, encoding='utf-8') as script:
        for raw in script:
            line = raw.strip()
            if not line or line.startswith('#'):
                continue
            print('> ' + line)
            try:
                ok = send(pipe, split_line(line)) and ok
            except Stuck:
                abandon_stuck(line)
    pipe.close()
    return 0 if ok else 1


def main():
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(errors='replace')
    global TIMEOUT
    if len(sys.argv) >= 3 and sys.argv[1] == '--timeout':
        TIMEOUT = float(sys.argv[2])
        del sys.argv[1:3]
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    if sys.argv[1] == 'start':
        parser = argparse.ArgumentParser(prog='engine.py start')
        parser.add_argument('--config', choices=['Debug', 'Release'], default=None,
                            help='build to run: Release by default, Debug with --gpu-validation')
        parser.add_argument('--camera', help='x,y,z[,pitch,yaw] start camera')
        parser.add_argument('--level', help='level from Levels')
        parser.add_argument('--resolution', default='1920x1080',
                            help='window and back buffer: WxH (window with a title) or desktop (borderless, whole monitor)')
        parser.add_argument('--nogui', action='store_true', help='no ImGui windows')
        parser.add_argument('--mouse', action='store_true', help='camera follows the mouse (no -nomouse)')
        parser.add_argument('--nowind', action='store_true', help='no wind: vegetation still, frames repeat to the pixel')
        parser.add_argument('--noparticles', action='store_true', help='no particles: frames repeat to the pixel')
        parser.add_argument('--pix', action='store_true', help='load the PIX GPU capturer: command pix takes captures')
        parser.add_argument('--gpu-validation', action='store_true',
                            help='Debug: GPU-based validation of the D3D12 debug layer (slow; for changes of the D3D12 layer)')
        return start(parser.parse_args(sys.argv[2:]))
    if sys.argv[1] == 'stop':
        return stop()
    if sys.argv[1] == 'run':
        if len(sys.argv) != 3:
            raise SystemExit('error: run needs a script file')
        return run_script(sys.argv[2])

    pipe = connect()
    if not pipe:
        raise SystemExit('error: no engine, start it: python Tools/engine.py start')
    try:
        ok = send(pipe, sys.argv[1:])
    except Stuck as stuck:
        abandon_stuck(str(stuck))
    pipe.close()
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
