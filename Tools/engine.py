"""Удалённое управление запущенным DMEngine — как консоль и Remote Control API в UE (docs/remote.md).

    python Tools/engine.py start [--config Release] [--camera x,y,z,pitch,yaw] [--level имя] [--nogui] [--mouse] [--nowind]
    python Tools/engine.py <команда ...>         одна команда, печатает ответ; код выхода 1 при error
    python Tools/engine.py run сценарий.txt       команды из файла по строке (# — комментарий), за одно подключение
    python Tools/engine.py stop                   quit, ожидание выхода, ошибки из log.txt

start запускает cmake-build-cli-<config>\\DMEngine.exe из корня проекта с -remote (канал \\\\.\\pipe\\DMEngine) и
-nomouse (без --mouse: камера не следует за мышью) и ждёт конца инициализации. Команды движка — help; путь снимка
(screenshot файл) считается от текущей папки. Сообщения скрипта — ASCII (правило Tools/).
"""
import argparse
import ctypes
import os
import re
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIPE = r'\\.\pipe\DMEngine'
PID_FILE = os.path.join(tempfile.gettempdir(), 'dmengine_remote.pid')


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


def command(pipe, line):
    """Команда и ответ: строки, последняя - ok или error: ..."""
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
    """Как Tools/run.ps1: число заглушек, ошибки, время инициализации, строки GPU average"""
    try:
        with open(os.path.join(ROOT, 'log.txt'), encoding='utf-8', errors='replace') as f:
            log = f.read().splitlines()
    except OSError:
        return
    placeholder = re.compile(r'placeholder( [a-z]+)? is used')
    count = sum(1 for line in log if placeholder.search(line))
    if count:
        print('Missing resources replaced by placeholders: %d' % count)
    report = re.compile(r'error|fail|exception|unknown|Total init|GPU average', re.I)
    for line in log:
        if report.search(line) and not placeholder.search(line):
            print(line)


def start(args):
    probe = connect(timeout=0)
    if probe:
        probe.close()
        raise SystemExit('error: an engine with -remote is already running')
    exe = os.path.join(ROOT, 'cmake-build-cli-%s' % args.config.lower(), 'DMEngine.exe')
    if not os.path.exists(exe):
        raise SystemExit('error: not found %s, build it first: Tools\\build.cmd %s' % (exe, args.config.lower()))
    engine_args = ['-remote']
    if not args.mouse:
        engine_args.append('-nomouse')
    if args.nogui:
        engine_args.append('-nogui')
    if args.nowind:
        engine_args.append('-nowind')
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
        with pipe:
            send(pipe, ['quit'])
    elif not (pid and process_alive(pid)):
        print('DMEngine is not running')
        print_log()
        return 0
    if pid and not wait_exit(pid, 20):
        print('error: DMEngine did not exit in 20 s')
        return 1
    print('DMEngine exited')
    print_log()
    return 0


def run_script(path):
    pipe = connect()
    if not pipe:
        raise SystemExit('error: no engine, start it: python Tools/engine.py start')
    ok = True
    with pipe, open(path, encoding='utf-8') as script:
        for raw in script:
            line = raw.strip()
            if not line or line.startswith('#'):
                continue
            print('> ' + line)
            ok = send(pipe, split_line(line)) and ok
    return 0 if ok else 1


def main():
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(errors='replace')
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    if sys.argv[1] == 'start':
        parser = argparse.ArgumentParser(prog='engine.py start')
        parser.add_argument('--config', choices=['Debug', 'Release'], default='Release')
        parser.add_argument('--camera', help='x,y,z[,pitch,yaw] start camera')
        parser.add_argument('--level', help='level from Levels')
        parser.add_argument('--nogui', action='store_true', help='no ImGui windows')
        parser.add_argument('--mouse', action='store_true', help='camera follows the mouse (no -nomouse)')
        parser.add_argument('--nowind', action='store_true', help='no wind: vegetation still, frames repeat to the pixel')
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
    with pipe:
        return 0 if send(pipe, sys.argv[1:]) else 1


if __name__ == '__main__':
    sys.exit(main())
