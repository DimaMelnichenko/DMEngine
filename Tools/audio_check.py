"""Проверка записи звука движка без наушников (команда audio record, docs/audio.md).

  python Tools/audio_check.py запись.wav [--png спектрограмма.png] [--seconds]

Печатает по каналам уровень (дБ FS, среднеквадратичный и пик), перегрузку, щелчки (скачки между отсчётами против
местного уровня и отдельно — на стыках блоков микшера по 480 отсчётов), спектральный центроид и доли полос (низ < 300 Гц,
середина, верх > 2 кГц); --seconds — то же по секундам (как меняется ветер); --png — спектрограмма (время — вправо, частота
— вверх по логарифму). Нужен numpy, для --png — Pillow. Сообщения — ASCII (Windows PowerShell 5.1, cmd).
"""
import argparse
import sys
import wave

import numpy as np

BLOCK = 480


def db(value):
    return 20.0 * np.log10(max(value, 1e-9))


def spectrum_stats(samples, rate):
    window = np.hanning(len(samples))
    power = np.abs(np.fft.rfft(samples * window)) ** 2
    freqs = np.fft.rfftfreq(len(samples), 1.0 / rate)
    total = power.sum() + 1e-30
    centroid = (freqs * power).sum() / total
    low = power[freqs < 300].sum() / total
    high = power[freqs > 2000].sum() / total
    return centroid, low, 1.0 - low - high, high


def clicks(samples):
    """Скачки между отсчётами, которых шум сам не даёт: |разность| против разностей в окне 10 мс вокруг"""
    diff = np.abs(np.diff(samples))
    if len(diff) < 2 * BLOCK:
        return 0, 1.0
    kernel = np.ones(BLOCK) / BLOCK
    local = np.sqrt(np.convolve(diff ** 2, kernel, mode='same')) + 1e-6
    outliers = int((diff > 8.0 * local).sum())
    boundaries = diff[BLOCK - 1::BLOCK]
    ratio = boundaries.mean() / (diff.mean() + 1e-12)
    return outliers, ratio


def report(name, samples, rate):
    rms = np.sqrt(np.mean(samples ** 2))
    peak = np.max(np.abs(samples))
    clipped = int((np.abs(samples) >= 0.999).sum())
    centroid, low, middle, high = spectrum_stats(samples, rate)
    outliers, boundary = clicks(samples)
    print('%-6s rms %6.1f dBFS, peak %6.1f dBFS, clipped %d, centroid %5.0f Hz, bands low %.2f mid %.2f high %.2f, '
          'clicks %d, block boundary jump x%.2f' % (name, db(rms), db(peak), clipped, centroid, low, middle, high, outliers, boundary))


def spectrogram(samples, rate, path):
    from PIL import Image
    size = 2048
    hop = 512
    frames = max((len(samples) - size) // hop, 1)
    window = np.hanning(size)
    columns = []
    for i in range(frames):
        chunk = samples[i * hop:i * hop + size]
        if len(chunk) < size:
            break
        columns.append(np.abs(np.fft.rfft(chunk * window)))
    magnitude = np.array(columns).T
    freqs = np.fft.rfftfreq(size, 1.0 / rate)
    # Частота по логарифму: 30 Гц … 20 кГц на 256 строк
    rows = np.geomspace(30.0, 20000.0, 256)
    image = np.array([magnitude[np.argmin(np.abs(freqs - f))] for f in rows])
    level = 20.0 * np.log10(image + 1e-9)
    level = np.clip((level - (level.max() - 80.0)) / 80.0, 0.0, 1.0)
    pixels = (level[::-1] * 255).astype(np.uint8)
    Image.fromarray(pixels, 'L').resize((max(pixels.shape[1], 512), 256)).save(path)
    print('written %s' % path)


def main():
    parser = argparse.ArgumentParser(description='Level, clipping, clicks and spectrum of an engine audio recording')
    parser.add_argument('wav')
    parser.add_argument('--png', help='spectrogram of the mid channel')
    parser.add_argument('--seconds', action='store_true', help='also per second')
    args = parser.parse_args()

    with wave.open(args.wav, 'rb') as file:
        if file.getsampwidth() != 2:
            sys.exit('expected 16-bit PCM')
        rate = file.getframerate()
        channels = file.getnchannels()
        data = np.frombuffer(file.readframes(file.getnframes()), dtype='<i2').astype(np.float64) / 32768.0
    data = data.reshape(-1, channels)
    print('%s: %d channels, %d Hz, %.2f s' % (args.wav, channels, rate, len(data) / rate))
    names = ['left', 'right'] if channels == 2 else ['ch%d' % c for c in range(channels)]
    for c in range(channels):
        report(names[c], data[:, c], rate)
    mid = data.mean(axis=1)
    if channels == 2:
        correlation = np.corrcoef(data[:, 0], data[:, 1])[0, 1]
        print('left-right correlation %.2f (1 - mono, 0 - independent)' % correlation)
    if args.seconds:
        for second in range(len(data) // rate):
            part = data[second * rate:(second + 1) * rate]
            centroid, low, middle, high = spectrum_stats(part.mean(axis=1), rate)
            print('  %3d s: left %6.1f dBFS, right %6.1f dBFS, centroid %5.0f Hz, high %.2f' % (
                second, db(np.sqrt(np.mean(part[:, 0] ** 2))), db(np.sqrt(np.mean(part[:, -1] ** 2))), centroid, high))
    if args.png:
        spectrogram(mid, rate, args.png)


if __name__ == '__main__':
    main()
