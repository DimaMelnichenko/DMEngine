# Шум для генераторов рельефа (gen_heightmap.py — долина карты, gen_far_terrain.py — дальние горы вокруг неё): градиентный
# шум Перлина, fbm, ridged multifractal Musgrave, smoothstep — векторно по массивам numpy
import math

import numpy as np


class Perlin:
    """Градиентный шум Перлина (quintic fade, Perlin 2002), значения ≈ −1…1, векторно по массивам координат"""

    def __init__(self, rng, period=256):
        angle = rng.uniform(0.0, 2.0 * math.pi, (period, period))
        self.gx, self.gy, self.period = np.cos(angle), np.sin(angle), period

    def __call__(self, x, y):
        ix, iy = np.floor(x).astype(np.int64), np.floor(y).astype(np.int64)
        fx, fy = x - ix, y - iy
        p = self.period

        def corner(ox, oy):
            gx = self.gx[(iy + oy) % p, (ix + ox) % p]
            gy = self.gy[(iy + oy) % p, (ix + ox) % p]
            return gx * (fx - ox) + gy * (fy - oy)

        u = fx * fx * fx * (fx * (fx * 6 - 15) + 10)
        v = fy * fy * fy * (fy * (fy * 6 - 15) + 10)
        top = corner(0, 0) + (corner(1, 0) - corner(0, 0)) * u
        bottom = corner(0, 1) + (corner(1, 1) - corner(0, 1)) * u
        return (top + (bottom - top) * v) * math.sqrt(2.0)


def fbm(noise, x, y, octaves, lacunarity=2.0, gain=0.5):
    total, amplitude, norm = 0.0, 1.0, 0.0
    for _ in range(octaves):
        total = total + noise(x, y) * amplitude
        norm += amplitude
        x, y = x * lacunarity, y * lacunarity
        amplitude *= gain
    return total / norm


def ridged(noise, x, y, octaves, lacunarity=2.0, gain=2.0, offset=1.0, h=1.0):
    """Ridged multifractal (F. K. Musgrave, «Texturing and Modeling», гл. 16): острые гребни из |шума|, каждая
    следующая октава сильнее на гребнях. Результат нормирован примерно в 0…1"""
    signal = (offset - np.abs(noise(x, y))) ** 2
    total, norm, weight = signal, 1.0, signal
    for octave in range(1, octaves):
        x, y = x * lacunarity, y * lacunarity
        weight = np.clip(signal * gain, 0.0, 1.0)
        signal = (offset - np.abs(noise(x, y))) ** 2 * weight
        amplitude = lacunarity ** (-octave * h)
        total = total + signal * amplitude
        norm += amplitude
    return total / norm


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)
