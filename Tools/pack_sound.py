"""Запись звука для движка: исходник (MP3, WAV, FLAC …) → Sounds\\<имя>.wav и строка таблицы Sounds (docs/audio.md, «Записи»).

  python Tools/pack_sound.py <исходник> --name <имя> [--author автор] [--license лицензия] [--source-url ссылка]
                             [--highpass Гц] [--threshold дБ] [--min-pause с] [--ffmpeg путь]

Запись приводится к виду микшера (AudioMixer): моно, 48 кГц, 16 бит; фильтр высоких частот (--highpass, по умолчанию 150 Гц)
убирает гул ветра в микрофоне и дальний транспорт. Громкость — к общему уровню: медиана громкости фраз — −18 дБ FS, пик — не
выше −1 дБ FS (слой певцов задаёт громкость сам). Фразы — по огибающей громкости (окна 10 мс): громче фона записи (20-й
процентиль) на --threshold дБ (по умолчанию 12), паузы короче --min-pause (0,3 с) не разрывают фразу, фраза короче 0,12 с —
щелчок, не фраза; поля 50 мс. Фразы, автор, лицензия и ссылка — в строку Sounds (по имени; есть — обновляется): у записей
CC BY автора указывать обязательно.

Декодирует ffmpeg: --ffmpeg, из PATH или распакованный в DownloadResources\\ffmpeg\\. Нужен numpy. Папка Sounds\\ — не
в git, как Textures\\ и Meshes\\: собирается этим скриптом из DownloadResources\\Sounds\\. Сообщения — ASCII (Windows
PowerShell 5.1, cmd).
"""
import argparse
import glob
import os
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import wave

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(ROOT, 'base.db3')
RATE = 48000

SCHEMA = """
CREATE TABLE IF NOT EXISTS Sounds (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    file TEXT NOT NULL,
    phrases TEXT,
    source TEXT,
    author TEXT,
    license TEXT
);
"""

def find_ffmpeg(explicit):
    """--ffmpeg, PATH или распакованный в DownloadResources\\ffmpeg\\ (архив сборки gyan.dev: 7z e <архив> "*/bin/ffmpeg.exe" -r)"""
    if explicit:
        return explicit
    found = shutil.which('ffmpeg')
    if found:
        return found
    local = glob.glob(os.path.join(ROOT, 'DownloadResources', 'ffmpeg*', '**', 'ffmpeg.exe'), recursive=True)
    return local[0] if local else None


def decode(source, target, highpass, ffmpeg):
    """Исходник → моно 48 кГц float32 WAV с фильтром высоких частот"""
    if not ffmpeg:
        sys.exit('error: ffmpeg is not found: add it to PATH or pass --ffmpeg <path to ffmpeg.exe>')
    filters = ['-af', 'highpass=f=%g' % highpass] if highpass > 0 else []
    subprocess.run([ffmpeg, '-v', 'error', '-y', '-i', source, '-ac', '1', '-ar', str(RATE)] + filters +
                   ['-c:a', 'pcm_f32le', target], check=True)


def read_float_wav(path):
    """WAV float32 или PCM16 → numpy float, каналы — в один"""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        sys.exit('error: not a WAV: %s' % path)
    position = 12
    fmt = None
    samples = None
    while position + 8 <= len(data):
        chunk = data[position:position + 4]
        size = int.from_bytes(data[position + 4:position + 8], 'little')
        body = data[position + 8:position + 8 + size]
        if chunk == b'fmt ':
            fmt = (int.from_bytes(body[0:2], 'little'), int.from_bytes(body[2:4], 'little'),
                   int.from_bytes(body[4:8], 'little'), int.from_bytes(body[14:16], 'little'))
            if fmt[0] == 0xFFFE and len(body) >= 26:
                fmt = (int.from_bytes(body[24:26], 'little'),) + fmt[1:]
        elif chunk == b'data':
            samples = body
        position += 8 + size + (size & 1)
    if fmt is None or samples is None:
        sys.exit('error: no fmt or data chunk in %s' % path)
    tag, channels, rate, bits = fmt
    if tag == 3 and bits == 32:
        values = np.frombuffer(samples, dtype='<f4').astype(np.float64)
    elif tag == 1 and bits == 16:
        values = np.frombuffer(samples, dtype='<i2').astype(np.float64) / 32768.0
    else:
        sys.exit('error: unsupported WAV format %d, %d bits' % (tag, bits))
    if rate != RATE:
        sys.exit('error: decoded rate %d, expected %d' % (rate, RATE))
    return values.reshape(-1, channels).mean(axis=1)


def find_phrases(samples, threshold_db, min_pause):
    window = RATE // 100
    count = len(samples) // window
    rms = np.sqrt(np.mean(samples[:count * window].reshape(count, window) ** 2, axis=1) + 1e-12)
    level = 20.0 * np.log10(rms)
    floor = np.percentile(level, 20)
    active = level > floor + threshold_db
    phrases = []
    start = None
    for i, on in enumerate(active):
        if on and start is None:
            start = i
        elif not on and start is not None:
            phrases.append([start, i])
            start = None
    if start is not None:
        phrases.append([start, count])
    # Паузы короче min_pause — внутри фразы
    merged = []
    for phrase in phrases:
        if merged and (phrase[0] - merged[-1][1]) * 0.01 < min_pause:
            merged[-1][1] = phrase[1]
        else:
            merged.append(phrase)
    pad = 5
    result = []
    for begin, end in merged:
        if (end - begin) * 0.01 < 0.12:
            continue
        result.append((max(begin - pad, 0) * window, min(end + pad, count) * window))
    # Поля не заходят на соседнюю фразу
    for i in range(1, len(result)):
        if result[i][0] < result[i - 1][1]:
            middle = (result[i][0] + result[i - 1][1]) // 2
            result[i - 1] = (result[i - 1][0], middle)
            result[i] = (middle, result[i][1])
    return result, floor


def main():
    parser = argparse.ArgumentParser(description='Sound recording for the engine: Sounds\\<name>.wav and a row of table Sounds')
    parser.add_argument('source')
    parser.add_argument('--name', required=True, help='name in table Sounds (the soundscape layer refers to it)')
    parser.add_argument('--author', default=None)
    parser.add_argument('--license', default=None)
    parser.add_argument('--source-url', default=None, help='where the recording is from')
    parser.add_argument('--highpass', type=float, default=150.0, help='Hz, 0 - off')
    parser.add_argument('--threshold', type=float, default=12.0, help='dB above the background of the recording')
    parser.add_argument('--min-pause', type=float, default=0.3, help='s: shorter pauses stay inside a phrase')
    parser.add_argument('--ffmpeg', default=None)
    args = parser.parse_args()

    if not os.path.exists(args.source):
        sys.exit('error: no file %s' % args.source)
    folder = os.path.join(ROOT, 'Sounds')
    os.makedirs(folder, exist_ok=True)
    decoded = os.path.join(tempfile.gettempdir(), 'dmengine_pack_sound.wav')
    decode(args.source, decoded, args.highpass, find_ffmpeg(args.ffmpeg))
    samples = read_float_wav(decoded)
    os.remove(decoded)

    phrases, floor = find_phrases(samples, args.threshold, args.min_pause)
    if not phrases:
        sys.exit('error: no phrases found (lower --threshold)')
    loudness = [np.sqrt(np.mean(samples[a:b] ** 2)) for a, b in phrases]
    gain = 10.0 ** (-18.0 / 20.0) / max(np.median(loudness), 1e-9)
    peak = np.max(np.abs(samples)) * gain
    if peak > 10.0 ** (-1.0 / 20.0):
        gain *= 10.0 ** (-1.0 / 20.0) / peak
    samples = np.clip(samples * gain, -1.0, 1.0)

    target = os.path.join(folder, args.name + '.wav')
    with wave.open(target, 'wb') as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes(np.round(samples * 32767.0).astype('<i2').tobytes())

    text = ';'.join('%.3f-%.3f' % (a / RATE, b / RATE) for a, b in phrases)
    db = sqlite3.connect(DB)
    db.executescript(SCHEMA)
    file = 'Sounds\\' + args.name + '.wav'
    if db.execute('SELECT id FROM Sounds WHERE name = ?', (args.name,)).fetchone():
        db.execute('UPDATE Sounds SET file = ?, phrases = ?, source = COALESCE(?, source), author = COALESCE(?, author), '
                   'license = COALESCE(?, license) WHERE name = ?',
                   (file, text, args.source_url, args.author, args.license, args.name))
    else:
        db.execute('INSERT INTO Sounds (name, file, phrases, source, author, license) VALUES (?, ?, ?, ?, ?, ?)',
                   (args.name, file, text, args.source_url, args.author, args.license))
    db.commit()

    lengths = [(b - a) / RATE for a, b in phrases]
    print('%.1f s, background %.1f dBFS, gain %+.1f dB' % (len(samples) / RATE, floor, 20 * np.log10(gain)))
    print('%d phrases, %.2f..%.2f s (median %.2f s), sound %.0f%% of the recording' % (
        len(phrases), min(lengths), max(lengths), float(np.median(lengths)), 100.0 * sum(lengths) * RATE / len(samples)))
    print('written %s, row Sounds.%s' % (target, args.name))


if __name__ == '__main__':
    main()
