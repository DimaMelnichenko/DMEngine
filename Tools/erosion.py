# Эрозия карты высот для Tools/gen_heightmap.py — как Erosion, Thermal и Flow в World Machine и Gaea. Высоты в метрах,
# карта — numpy высота × ширина (строки сверху вниз), шаг сетки cell метров. Нужен numpy.
#   hydraulic_erosion — капли воды размывают склоны и откладывают грунт (Beyer 2015);
#   thermal_erosion   — осыпание склонов круче угла естественного откоса (Musgrave, Kolb, Mace 1989);
#   flow_accumulation — площадь водосбора по стоку D8 после заполнения низин (Priority-Flood, Barnes et al. 2014).
import heapq

import numpy as np

# Восемь соседей: сдвиг (строка, столбец) и расстояние в шагах сетки
NEIGHBOURS = [(-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1)]
DISTANCES = [np.hypot(dy, dx) for dy, dx in NEIGHBOURS]


def hydraulic_erosion(height, cell, droplets, rng, batch=65536, lifetime=80, inertia=0.3, capacity=1.0,
                      min_slope=0.01, erode_speed=0.1, deposit_speed=0.2, evaporation=0.01, gravity=4.0,
                      radius=5, rain=None):
    """Капли по H. T. Beyer, «Implementation of a method for hydraulic erosion» (2015), как у S. Lague.

    Капля начинает в случайной точке, идёт вниз по билинейному градиенту с инерцией inertia, на каждом шаге — один
    тексель. Ёмкость переноса — max(−Δh, min_slope) · скорость · вода · capacity (Δh — перепад за шаг в метрах,
    делённый на шаг, то есть уклон); лишний грунт откладывается билинейно в четыре угла ячейки (при подъёме — заполняет
    яму), недостающий размывается кистью радиуса radius. Вода испаряется на evaporation за шаг, скорость растёт вниз по
    склону (gravity). Размыв за шаг не глубже перепада, поэтому capacity · erode_speed ≪ 1: иначе каждая капля
    срезает свой путь до ровного, и гребни становятся плато. Капли идут пакетами по batch штук: внутри шага капли друг
    друга не видят, как на GPU. Карта на время расчёта продолжена за край на кисть: капли доходят до края и уходят за
    него, без нетронутой рамки. Возвращает (высота, размыв, отложения) в метрах; размыв и отложения — итоговое
    изменение высоты (понижение и повышение), а не сумма за все шаги. rain — карта того же размера, где капли
    начинаются чаще (вероятность пропорциональна значению); None — всюду поровну."""
    source = height.astype(np.float64)
    pad = radius + 1
    padded = np.pad(source, pad, mode='edge')
    rows, cols = padded.shape
    flat = padded.ravel().copy()

    # Кисть размыва: веса убывают к краю, в сумме 1
    oy, ox = np.mgrid[-radius:radius + 1, -radius:radius + 1]
    inside = ox * ox + oy * oy <= radius * radius
    oy, ox = oy[inside], ox[inside]
    brush = np.maximum(radius - np.hypot(ox, oy), 0.0)
    brush /= brush.sum()
    if rain is not None:
        rain_cells = np.maximum(rain.astype(np.float64), 0.0).ravel()
        rain_cells /= rain_cells.sum()

    def sample(px, py):
        # Высота и градиент (на тексель) билинейно
        ix = px.astype(np.int64)
        iy = py.astype(np.int64)
        fx, fy = px - ix, py - iy
        i = iy * cols + ix
        h00, h10, h01, h11 = flat[i], flat[i + 1], flat[i + cols], flat[i + cols + 1]
        gx = (h10 - h00) * (1 - fy) + (h11 - h01) * fy
        gy = (h01 - h00) * (1 - fx) + (h11 - h10) * fx
        h = h00 * (1 - fx) * (1 - fy) + h10 * fx * (1 - fy) + h01 * (1 - fx) * fy + h11 * fx * fy
        return h, gx / cell, gy / cell, i, fx, fy

    remaining = droplets
    while remaining > 0:
        n = min(batch, remaining)
        remaining -= n
        # Капли — по исходной карте (без продолжения за край), по карте дождя
        if rain is None:
            px = rng.uniform(pad, cols - 1 - pad, n)
            py = rng.uniform(pad, rows - 1 - pad, n)
        else:
            start = rng.choice(rain_cells.size, n, p=rain_cells)
            py = np.minimum(pad + start // source.shape[1] + rng.random(n), rows - 1 - pad - 1e-3)
            px = np.minimum(pad + start % source.shape[1] + rng.random(n), cols - 1 - pad - 1e-3)
        dx = np.zeros(n)
        dy = np.zeros(n)
        speed = np.ones(n)
        water = np.ones(n)
        sediment = np.zeros(n)
        alive = np.ones(n, dtype=bool)
        for _ in range(lifetime):
            h, gx, gy, i, fx, fy = sample(px, py)
            dx = dx * inertia - gx * (1 - inertia)
            dy = dy * inertia - gy * (1 - inertia)
            length = np.hypot(dx, dy)
            # На ровном месте — случайное направление
            still = length < 1e-9
            if still.any():
                angle = rng.uniform(0, 2 * np.pi, still.sum())
                dx[still], dy[still] = np.cos(angle), np.sin(angle)
                length[still] = 1.0
            dx /= length
            dy /= length
            nx, ny = px + dx, py + dy
            # За продолжением карты капля пропадает вместе с грунтом
            alive &= (nx >= radius) & (nx < cols - 1 - radius) & (ny >= radius) & (ny < rows - 1 - radius)
            nx = np.where(alive, nx, px)
            ny = np.where(alive, ny, py)
            nh = sample(nx, ny)[0]
            dh = np.where(alive, nh - h, 0.0)

            cap = np.maximum(-dh / cell, min_slope) * speed * water * capacity
            uphill = dh > 0
            deposit = np.where(uphill, np.minimum(dh, sediment),
                               np.where(sediment > cap, (sediment - cap) * deposit_speed, 0.0))
            erode = np.where(~uphill & (sediment <= cap),
                             np.minimum((cap - sediment) * erode_speed, -dh), 0.0)
            deposit = np.where(alive, deposit, 0.0)
            erode = np.where(alive, erode, 0.0)

            size = rows * cols
            added = np.bincount(np.concatenate([i, i + 1, i + cols, i + cols + 1]),
                                weights=np.concatenate([deposit * (1 - fx) * (1 - fy), deposit * fx * (1 - fy),
                                                        deposit * (1 - fx) * fy, deposit * fx * fy]), minlength=size)
            ix = px.astype(np.int64)
            iy = py.astype(np.int64)
            cells = ((iy[:, None] + oy) * cols + ix[:, None] + ox).ravel()
            removed = np.bincount(cells, weights=(erode[:, None] * brush).ravel(), minlength=size)
            flat += added - removed
            sediment += erode - deposit

            speed = np.sqrt(np.maximum(speed * speed - dh / cell * gravity, 0.0))
            water *= 1.0 - evaporation
            px, py = nx, ny
            if not alive.any():
                break
    result = flat.reshape(rows, cols)[pad:-pad, pad:-pad]
    change = result - source
    return result, np.maximum(-change, 0.0), np.maximum(change, 0.0)


def thermal_erosion(height, cell, talus_angle=38.0, iterations=40, rate=0.25):
    """Осыпание: где перепад к соседу больше tan(talus_angle) · расстояние, доля rate излишка сползает к более низким
    соседям пропорционально их излишку (F. K. Musgrave, C. E. Kolb, R. S. Mace, «The synthesis and rendering of
    eroded fractal terrains», 1989; вариант J. Olsen, 2004). За краем карты — продолжение крайнего текселя, грунт
    с карты не уходит. Возвращает (высота, толщина осыпи в метрах — сколько грунта пришло в клетку)."""
    h = height.astype(np.float64).copy()
    rows, cols = h.shape
    talus = np.zeros_like(h)
    limits = [np.tan(np.radians(talus_angle)) * d * cell for d in DISTANCES]
    for _ in range(iterations):
        padded = np.pad(h, 1, mode='edge')
        excess = []
        for (dy, dx), limit in zip(NEIGHBOURS, limits):
            neighbour = padded[1 + dy:1 + dy + rows, 1 + dx:1 + dx + cols]
            excess.append(np.maximum(h - neighbour - limit, 0.0))
        total = sum(excess)
        largest = np.maximum.reduce(excess)
        moving = total > 0
        if not moving.any():
            break
        # Уходит доля наибольшего излишка: так склон не перескакивает через угол откоса
        out = rate * largest
        share = np.where(moving, out / np.where(moving, total, 1.0), 0.0)
        gained = np.zeros((rows + 2, cols + 2))
        for (dy, dx), e in zip(NEIGHBOURS, excess):
            gained[1 + dy:1 + dy + rows, 1 + dx:1 + dx + cols] += e * share
        # Грунт, ушедший за край, возвращается в крайние клетки
        gained[1, :] += gained[0, :]
        gained[-2, :] += gained[-1, :]
        gained[:, 1] += gained[:, 0]
        gained[:, -2] += gained[:, -1]
        inner = gained[1:-1, 1:-1]
        h += inner - out
        talus += inner
    return h, talus


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
