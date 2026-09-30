"""Скачивание ассета Poly Haven (CC0) архивом .blend.zip в DownloadResources\\ — как кнопка Download на сайте.

    python Tools/polyhaven_download.py <ассет> [--resolution 2k] [--list] [--force]

Poly Haven отдаёт файлы через API (https://api.polyhaven.com/files/<ассет>): у записи blend/<разрешение>/blend — ссылка
на .blend и список include (текстуры путями относительно .blend, обычно textures/...). Скрипт складывает их в
DownloadResources\\<ассет>_<разрешение>.blend.zip с той же раскладкой, что у архива с сайта: его читают
Tools/export_polyhaven.py (--archive) и Tools/blender_fir.py. Так исходники моделей восстанавливаются на новой машине
командами из docs/models.md (пункт «где хранить исходники ассетов» в TODO.md). --list показывает разрешения и размеры.
Сообщения скрипта — ASCII (правило Tools/).
"""
import argparse
import json
import os
import sys
import tempfile
import urllib.request
import zipfile

API = 'https://api.polyhaven.com/files/%s'
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, 'DownloadResources')


# Cloudflare у Poly Haven отвечает 403 на стандартный User-Agent urllib — представляемся явно
USER_AGENT = 'DMEngine-Tools/1.0 (Tools/polyhaven_download.py)'


def request(url):
    return urllib.request.Request(url, headers={'User-Agent': USER_AGENT})


def fetch_json(url):
    with urllib.request.urlopen(request(url), timeout=60) as response:
        return json.load(response)


def download(url, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with urllib.request.urlopen(request(url), timeout=300) as response, open(path, 'wb') as out:
        while True:
            chunk = response.read(1 << 20)
            if not chunk:
                break
            out.write(chunk)


def main():
    parser = argparse.ArgumentParser(description='Download a Poly Haven model as <asset>_<res>.blend.zip into DownloadResources')
    parser.add_argument('asset', help='Poly Haven asset id, e.g. rock_moss_set_01')
    parser.add_argument('--resolution', default='2k', help='texture resolution of the .blend variant (1k, 2k, 4k, 8k)')
    parser.add_argument('--list', action='store_true', help='print available resolutions and sizes, do not download')
    parser.add_argument('--force', action='store_true', help='overwrite an existing archive')
    args = parser.parse_args()

    try:
        files = fetch_json(API % args.asset)
    except Exception as error:
        raise SystemExit('error: cannot query Poly Haven API for %s: %s' % (args.asset, error))
    blends = files.get('blend')
    if not blends:
        raise SystemExit('error: %s has no .blend downloads (keys: %s)' % (args.asset, ', '.join(sorted(files))))

    if args.list:
        for resolution, entry in blends.items():
            blend = entry['blend']
            total = blend['size'] + sum(item['size'] for item in blend.get('include', {}).values())
            print('%-4s %6.1f MB  %d texture(s)  %s' % (resolution, total / 1048576.0, len(blend.get('include', {})), blend['url']))
        return 0

    if args.resolution not in blends:
        raise SystemExit('error: no %s variant, available: %s' % (args.resolution, ', '.join(blends)))
    blend = blends[args.resolution]['blend']
    archive = os.path.join(OUT_DIR, '%s_%s.blend.zip' % (args.asset, args.resolution))
    if os.path.exists(archive) and not args.force:
        print('exists: %s (use --force to overwrite)' % archive)
        return 0

    folder = tempfile.mkdtemp(prefix='dm_polyhaven_dl_')
    items = [(os.path.basename(blend['url']), blend['url'], blend['size'])]
    items += [(name.replace('\\', '/'), item['url'], item['size']) for name, item in blend.get('include', {}).items()]
    total = sum(size for _, _, size in items)
    print('%s %s: %d file(s), %.1f MB (CC0, https://polyhaven.com/a/%s)' % (args.asset, args.resolution, len(items), total / 1048576.0, args.asset))
    for name, url, size in items:
        print('  %s (%.1f MB)' % (name, size / 1048576.0))
        download(url, os.path.join(folder, *name.split('/')))

    os.makedirs(OUT_DIR, exist_ok=True)
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as zf:
        for name, _, _ in items:
            zf.write(os.path.join(folder, *name.split('/')), name)
    print('written %s (%.1f MB)' % (archive, os.path.getsize(archive) / 1048576.0))
    return 0


if __name__ == '__main__':
    sys.exit(main())
