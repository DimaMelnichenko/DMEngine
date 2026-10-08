# Водосбор карты высот для Tools/gen_heightmap.py (превью) — как Flow в World Machine и Gaea. Высоты в метрах, карта —
# numpy высота × ширина (строки сверху вниз), шаг сетки cell метров. Нужен numpy. Сама эрозия (капли и осыпание) —
# в движке, первая ступень конвейера рельефа и воды (TerrainErosion, docs/terrain.md, «Эрозия»).
#   flow_accumulation — площадь водосбора по стоку D8 после заполнения низин (Priority-Flood, Barnes et al. 2014).
import heapq

import numpy as np

# Восемь соседей: сдвиг (строка, столбец) и расстояние в шагах сетки
NEIGHBOURS = [(-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1)]
DISTANCES = [np.hypot(dy, dx) for dy, dx in NEIGHBOURS]


def fill_depressions(height, epsilon=1e-4):
    """Priority-Flood с малым уклоном (R. Barnes, C. Lehman, D. Mulla, 2014): низины заполняются так, чтобы из каждой
    клетки был путь вниз к краю карты с перепадом не меньше epsilon на шаг. Возвращает заполненную карту"""
    rows, cols = height.shape
    h = height.astype(np.float64).ravel().tolist()
    filled = list(h)
    closed = bytearray(rows * cols)
    heap = []
    for r in range(rows):
        for c in (0, cols - 1):
            i = r * cols + c
            closed[i] = 1
            heap.append((h[i], i))
    for c in range(1, cols - 1):
        for r in (0, rows - 1):
            i = r * cols + c
            closed[i] = 1
            heap.append((h[i], i))
    heapq.heapify(heap)
    push, pop = heapq.heappush, heapq.heappop
    while heap:
        z, i = pop(heap)
        r, c = divmod(i, cols)
        for dy, dx in NEIGHBOURS:
            rr, cc = r + dy, c + dx
            if 0 <= rr < rows and 0 <= cc < cols:
                j = rr * cols + cc
                if not closed[j]:
                    closed[j] = 1
                    value = h[j] if h[j] > z + epsilon else z + epsilon
                    filled[j] = value
                    push(heap, (value, j))
    return np.asarray(filled).reshape(rows, cols)


def flow_accumulation(height, cell):
    """Площадь водосбора каждой клетки, м²: вода стекает в соседа с наибольшим уклоном (D8, O'Callaghan и Mark 1984)
    по карте с заполненными низинами. Клетки обходятся от высоких к низким, каждая отдаёт накопленное получателю.
    Возвращает (водосбор, заполненная карта)"""
    rows, cols = height.shape
    filled = fill_depressions(height)
    padded = np.pad(filled, 1, mode='edge')
    best = np.zeros((rows, cols))
    receiver = np.arange(rows * cols).reshape(rows, cols)
    for (dy, dx), d in zip(NEIGHBOURS, DISTANCES):
        neighbour = padded[1 + dy:1 + dy + rows, 1 + dx:1 + dx + cols]
        slope = (filled - neighbour) / d
        rr = np.clip(np.arange(rows)[:, None] + dy, 0, rows - 1)
        cc = np.clip(np.arange(cols)[None, :] + dx, 0, cols - 1)
        better = slope > best
        best = np.where(better, slope, best)
        receiver = np.where(better, rr * cols + cc, receiver)
    receiver = receiver.ravel()
    order = np.argsort(filled.ravel(), kind='stable')[::-1].tolist()
    recv = receiver.tolist()
    area = [cell * cell] * (rows * cols)
    for i in order:
        r = recv[i]
        if r != i:
            area[r] += area[i]
    return np.asarray(area).reshape(rows, cols), filled
