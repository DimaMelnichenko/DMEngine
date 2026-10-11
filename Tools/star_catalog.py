"""Звёзды ночного неба в base.db3 (таблица Stars) из Yale Bright Star Catalogue, 5-е изд. (Hoffleit, Warren 1991; CDS
VizieR V/50): ~9100 звёзд до 6,5 величины — всё, что видно глазом на тёмном небе. Их рисует фон неба (SkyAtmosphere,
Shaders/sky_stars.vs, docs/sky.md, «Ночь»).

  python Tools/star_catalog.py [--catalog DownloadResources/bsc5/catalog.gz] [--download]

Файл каталога — фиксированные колонки (ReadMe V/50): HR 1-4, имя 5-14, RA J2000 76-83, Dec J2000 84-90, V 103-107,
B-V 110-114. Строки без координат или величины (новые, туманности, убранные из каталога объекты) пропускаются; без B-V —
цвет звезды класса G (0,65). Таблица пересоздаётся целиком. --download скачивает каталог с CDS, если файла нет.
Сообщения — ASCII (Windows PowerShell 5.1, cmd).
"""
import argparse
import gzip
import os
import sqlite3
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(ROOT, 'base.db3')
DEFAULT_CATALOG = os.path.join(ROOT, 'DownloadResources', 'bsc5', 'catalog.gz')
URL = 'https://cdsarc.cds.unistra.fr/ftp/V/50/catalog.gz'
SOLAR_COLOR_INDEX = 0.65

SCHEMA = """
DROP TABLE IF EXISTS Stars;
CREATE TABLE Stars (
    hr INTEGER PRIMARY KEY,
    name TEXT NOT NULL DEFAULT '',
    ra REAL NOT NULL,
    dec REAL NOT NULL,
    magnitude REAL NOT NULL,
    color_index REAL NOT NULL
);
"""


def field(line, first, last):
    """Колонки first..last (с 1, включительно, как в ReadMe); пусто — None."""
    text = line[first - 1:last].strip()
    return text if text else None


def parse(line):
    hr = field(line, 1, 4)
    ra_h, ra_m, ra_s = field(line, 76, 77), field(line, 78, 79), field(line, 80, 83)
    de_sign, de_d, de_m, de_s = line[83:84], field(line, 85, 86), field(line, 87, 88), field(line, 89, 90)
    vmag = field(line, 103, 107)
    if None in (hr, ra_h, ra_m, ra_s, de_d, de_m, de_s, vmag):
        return None
    ra = 15.0 * (int(ra_h) + int(ra_m) / 60.0 + float(ra_s) / 3600.0)
    dec = int(de_d) + int(de_m) / 60.0 + int(de_s) / 3600.0
    if de_sign == '-':
        dec = -dec
    color = field(line, 110, 114)
    name = field(line, 5, 14) or ''
    return int(hr), ' '.join(name.split()), ra, dec, float(vmag), float(color) if color else SOLAR_COLOR_INDEX


def main():
    parser = argparse.ArgumentParser(description='Bright star catalogue (BSC5) -> table Stars')
    parser.add_argument('--catalog', default=DEFAULT_CATALOG)
    parser.add_argument('--download', action='store_true', help='download the catalogue from CDS if it is missing')
    args = parser.parse_args()

    if not os.path.exists(args.catalog):
        if not args.download:
            sys.exit('no catalogue %s; run with --download or pass --catalog' % args.catalog)
        os.makedirs(os.path.dirname(args.catalog), exist_ok=True)
        print('downloading %s' % URL)
        urllib.request.urlretrieve(URL, args.catalog)

    opener = gzip.open if args.catalog.endswith('.gz') else open
    with opener(args.catalog, 'rt', encoding='ascii', errors='replace') as source:
        lines = source.read().splitlines()
    stars = [star for star in (parse(line) for line in lines) if star]

    db = sqlite3.connect(DB)
    db.executescript(SCHEMA)
    db.executemany('INSERT INTO Stars (hr, name, ra, dec, magnitude, color_index) VALUES (?, ?, ?, ?, ?, ?)', stars)
    db.commit()
    brightest = min(stars, key=lambda star: star[4])
    print('Stars: %d of %d lines; brightest HR %d %s V=%.2f; faintest V=%.2f' %
          (len(stars), len(lines), brightest[0], brightest[1], brightest[4], max(star[4] for star in stars)))


if __name__ == '__main__':
    main()
