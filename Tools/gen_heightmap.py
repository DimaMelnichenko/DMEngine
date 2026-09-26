# Генерирует тестовую карту высот террейна: гладкие холмы из суммы синусоид (7 октав), DDS R16_UNORM.
# Результат детерминирован (seed 7). Без аргументов пишет Textures\terrain\heightmap.dds 1024×1024 —
# файл, который base.db3 ждёт для текстуры t_heightmap. Запускать из корня проекта:
#   python Tools/gen_heightmap.py [путь] [размер]
import array
import math
import os
import random
import struct
import sys

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join('Textures', 'terrain', 'heightmap.dds')
size = int(sys.argv[2]) if len(sys.argv) > 2 else 1024

random.seed(7)
waves = []
amp = 1.0
freq = 2.0
for octave in range(7):
    for _ in range(3):
        angle = random.uniform(0, math.pi * 2)
        waves.append((math.cos(angle) * freq, math.sin(angle) * freq, random.uniform(0, math.pi * 2), amp))
    amp *= 0.5
    freq *= 2.0

values = array.array('d', [0.0]) * (size * size)
lo, hi = 1e9, -1e9
for y in range(size):
    v = y / size * 2 * math.pi
    row = y * size
    for x in range(size):
        u = x / size * 2 * math.pi
        h = 0.0
        for fx, fy, phase, a in waves:
            h += a * math.sin(u * fx + v * fy + phase)
        values[row + x] = h
        lo = min(lo, h)
        hi = max(hi, h)

pixels = array.array('H', (int((h - lo) / (hi - lo) * 65535) for h in values))

# Заголовок DDS + расширение DX10: DXGI_FORMAT_R16_UNORM (56), TEXTURE2D
header = struct.pack('<4sIIIIIII44sIIIIIIIIIIIII',
    b'DDS ', 124, 0x100F, size, size, size * 2, 0, 0, b'\0' * 44,
    32, 0x4, struct.unpack('<I', b'DX10')[0], 0, 0, 0, 0, 0,
    0x1000, 0, 0, 0, 0)
dx10 = struct.pack('<IIIII', 56, 3, 0, 1, 0)

os.makedirs(os.path.dirname(out) or '.', exist_ok=True)
with open(out, 'wb') as f:
    f.write(header)
    f.write(dx10)
    f.write(pixels.tobytes())
print('written', out, size)
