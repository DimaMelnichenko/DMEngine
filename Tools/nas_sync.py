"""Ресурсы не из git (Textures, Meshes, Sounds, DownloadResources) — копия на NAS по SSH: сравнить, залить, забрать.

  python Tools/nas_sync.py [status]                — чего нет на NAS, чего нет здесь, что различается
  python Tools/nas_sync.py push [--dry-run]        — залить на NAS недостающее и то, что здесь новее
  python Tools/nas_sync.py pull [--dry-run]        — забрать с NAS недостающее и то, что там новее
  общие ключи: [--folder Sounds ...] [--host dimok@192.168.1.80] [--key ~/.ssh/ключ] [--remote DMEngineProject]

Хранилище исходников ассетов — NAS (решение Dimok 2026-10-19, TODO.md «Решить или проверить»): скачанные архивы
(DownloadResources), собранные из них данные движка (Textures, Meshes, Sounds) и итог эрозии (Textures\\terrain\\eroded,
far_eroded) — на обеих машинах одни и те же: эрозия на разных GPU даёт чуть разный рельеф и русла. Не сравниваются кэш
правил слоёв и масок (Textures\\terrain\\generated — движок пересобирает его сам) и служебные папки Synology (@eaDir,
#recycle). Файлы не удаляются ни здесь, ни на NAS.

Сравнение — по размеру и времени правки (scp -p переносит время); одинаковый размер при разном времени — по MD5 с обеих
сторон (одинаковые — «то же содержимое», push выравнивает время на NAS, pull — здесь). Запускать из PowerShell или cmd:
SSH из песочницы Git Bash в локальную сеть не ходит. На NAS выключен SFTP — копирование старым протоколом SCP (scp -O).
Сообщения — ASCII (Windows PowerShell 5.1, cmd).
"""
import argparse
import hashlib
import os
import shlex
import subprocess
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FOLDERS = ['Textures', 'Meshes', 'Sounds', 'DownloadResources']
# Не сравниваются: кэш правил (пересобирает движок) и служебное Synology
SKIP_PREFIXES = ['Textures/terrain/generated/']
SKIP_PARTS = ['@eaDir', '#recycle']
DEFAULT_HOST = os.environ.get('DMENGINE_NAS', 'dimok@192.168.1.80')
DEFAULT_KEY = os.path.join(os.path.expanduser('~'), '.ssh', 'id_ed25519_gitlab')
BATCH = 40  # файлов на один вызов scp (длина командной строки Windows)


def skipped(path):
    return any(path.startswith(prefix) for prefix in SKIP_PREFIXES) or any(part in SKIP_PARTS for part in path.split('/'))


def ssh_options(args):
    options = ['-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10']
    if args.key and os.path.exists(args.key):
        options += ['-i', args.key, '-o', 'IdentitiesOnly=yes']
    return options


def ssh(args, command):
    result = subprocess.run(['ssh'] + ssh_options(args) + [args.host, command], capture_output=True)
    if result.returncode != 0:
        sys.exit('ssh failed (%d): %s' % (result.returncode, result.stderr.decode('utf-8', 'replace').strip()))
    return result.stdout.decode('utf-8', 'replace')


def remote_quote(path):
    return shlex.quote(path)


def local_files(folders):
    files = {}
    for folder in folders:
        base = os.path.join(ROOT, folder)
        for directory, _, names in os.walk(base):
            for name in names:
                full = os.path.join(directory, name)
                path = os.path.relpath(full, ROOT).replace('\\', '/')
                if skipped(path):
                    continue
                stat = os.stat(full)
                files[path] = (stat.st_size, int(stat.st_mtime))
    return files


def remote_files(args, folders):
    root = remote_quote(args.remote)
    existing = ' '.join(remote_quote(folder) for folder in folders)
    command = ('cd %s && for d in %s; do [ -d "$d" ] && find "$d" -type f -exec stat -c "%%s %%Y %%n" {} +; done; true'
               % (root, existing))
    files = {}
    for line in ssh(args, command).splitlines():
        parts = line.split(' ', 2)
        if len(parts) != 3:
            continue
        path = parts[2]
        if not skipped(path):
            files[path] = (int(parts[0]), int(parts[1]))
    return files


def local_md5(path):
    digest = hashlib.md5()
    with open(os.path.join(ROOT, path), 'rb') as file:
        for chunk in iter(lambda: file.read(1 << 20), b''):
            digest.update(chunk)
    return digest.hexdigest()


def remote_md5(args, paths):
    sums = {}
    for start in range(0, len(paths), BATCH):
        chunk = paths[start:start + BATCH]
        output = ssh(args, 'cd %s && md5sum %s' % (remote_quote(args.remote), ' '.join(remote_quote(p) for p in chunk)))
        for line in output.splitlines():
            digest, _, path = line.partition('  ')
            sums[path.lstrip('*')] = digest
    return sums


def compare(args, folders):
    """Сравнение: (только здесь, только на NAS, здесь новее, на NAS новее, то же содержимое при разном времени)"""
    local = local_files(folders)
    remote = remote_files(args, folders)
    only_local = sorted(set(local) - set(remote))
    only_remote = sorted(set(remote) - set(local))
    local_newer, remote_newer, same_content = [], [], []
    candidates = []
    for path in sorted(set(local) & set(remote)):
        (lsize, ltime), (rsize, rtime) = local[path], remote[path]
        if lsize == rsize and abs(ltime - rtime) <= 2:
            continue
        if lsize == rsize:
            candidates.append(path)
        elif ltime >= rtime:
            local_newer.append(path)
        else:
            remote_newer.append(path)
    if candidates:
        print('checking %d files of the same size by MD5...' % len(candidates))
        sums = remote_md5(args, candidates)
        for path in candidates:
            if sums.get(path) == local_md5(path):
                same_content.append(path)
            elif local[path][1] >= remote[path][1]:
                local_newer.append(path)
            else:
                remote_newer.append(path)
    return local, remote, only_local, only_remote, local_newer, remote_newer, same_content


def megabytes(paths, sizes):
    return sum(sizes[p][0] for p in paths) / (1 << 20)


def print_list(title, paths, sizes):
    if not paths:
        return
    print('%s: %d files, %.1f MB' % (title, len(paths), megabytes(paths, sizes)))
    for path in paths[:40]:
        print('  %s' % path)
    if len(paths) > 40:
        print('  ... and %d more' % (len(paths) - 40))


def by_directory(paths):
    groups = defaultdict(list)
    for path in paths:
        groups[os.path.dirname(path)].append(path)
    return groups


def scp(args, sources, target):
    command = ['scp', '-O', '-p', '-q'] + ssh_options(args) + sources + [target]
    if subprocess.run(command).returncode != 0:
        sys.exit('scp failed: %s -> %s' % (sources[0], target))


def push(args, paths, same_content, local):
    groups = by_directory(paths)
    if groups:
        ssh(args, 'cd %s && mkdir -p %s' % (remote_quote(args.remote), ' '.join(remote_quote(d) for d in groups)))
    done = 0
    for directory, files in sorted(groups.items()):
        target = '%s:%s/' % (args.host, remote_quote('%s/%s' % (args.remote, directory)))
        for start in range(0, len(files), BATCH):
            chunk = files[start:start + BATCH]
            scp(args, [os.path.join(ROOT, p) for p in chunk], target)
            done += len(chunk)
            print('pushed %d of %d' % (done, len(paths)))
    # То же содержимое — время на NAS как здесь (следующее сравнение без MD5)
    for start in range(0, len(same_content), BATCH):
        chunk = same_content[start:start + BATCH]
        ssh(args, 'cd %s && %s' % (remote_quote(args.remote), '; '.join(
            'touch -m -d @%d %s' % (local[p][1], remote_quote(p)) for p in chunk)))


def pull(args, paths, same_content, remote):
    done = 0
    for directory, files in sorted(by_directory(paths).items()):
        os.makedirs(os.path.join(ROOT, directory), exist_ok=True)
        for start in range(0, len(files), BATCH):
            chunk = files[start:start + BATCH]
            sources = ['%s:%s' % (args.host, remote_quote('%s/%s' % (args.remote, p))) for p in chunk]
            scp(args, sources, os.path.join(ROOT, directory) + os.sep)
            done += len(chunk)
            print('pulled %d of %d' % (done, len(paths)))
    for path in same_content:
        stamp = remote[path][1]
        os.utime(os.path.join(ROOT, path), (stamp, stamp))


def main():
    parser = argparse.ArgumentParser(description='Resources not in git - a copy on the NAS over SSH')
    parser.add_argument('command', nargs='?', default='status', choices=['status', 'push', 'pull'])
    parser.add_argument('--folder', action='append', choices=FOLDERS, help='only these folders (repeatable)')
    parser.add_argument('--host', default=DEFAULT_HOST, help='user@host of the NAS (or env DMENGINE_NAS)')
    parser.add_argument('--key', default=DEFAULT_KEY, help='SSH private key')
    parser.add_argument('--remote', default='DMEngineProject', help='project folder on the NAS, relative to the home')
    parser.add_argument('--dry-run', action='store_true', help='push / pull: only print what would be copied')
    args = parser.parse_args()
    folders = args.folder or FOLDERS

    local, remote, only_local, only_remote, local_newer, remote_newer, same_content = compare(args, folders)
    print('here: %d files, NAS: %d files (%s)' % (len(local), len(remote), ', '.join(folders)))
    print_list('only here', only_local, local)
    print_list('only on the NAS', only_remote, remote)
    print_list('newer here', local_newer, local)
    print_list('newer on the NAS', remote_newer, remote)
    if same_content:
        print('same content, different time: %d files' % len(same_content))
    if not (only_local or only_remote or local_newer or remote_newer):
        print('in sync')

    if args.command == 'push':
        paths = only_local + local_newer
        print('push: %d files, %.1f MB%s' % (len(paths), megabytes(paths, local), ' (dry run)' if args.dry_run else ''))
        if not args.dry_run:
            push(args, paths, same_content, local)
    elif args.command == 'pull':
        paths = only_remote + remote_newer
        print('pull: %d files, %.1f MB%s' % (len(paths), megabytes(paths, remote), ' (dry run)' if args.dry_run else ''))
        if not args.dry_run:
            pull(args, paths, same_content, remote)


if __name__ == '__main__':
    main()
