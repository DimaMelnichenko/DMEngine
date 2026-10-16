"""Проверка правки рендера одной командой: снимки и «GPU average» контрольных камер (docs/d3d12.md §4) против эталона —
как Automation и Gauntlet в UE.

    python Tools/check_frames.py reference [--config Debug|Release] [--runs 2]
    python Tools/check_frames.py [check] [--config Debug|Release] [--gpu-validation] [--gpu-threshold 10] [--allowed 32]

reference — эталон текущей сборки (до правки или после принятой правки): --runs запусков движка (engine.py start --nogui
--nowind --noparticles --freezetime --nocolorgrading: цветокоррекция уровня кадр не меняет), на каждой из четырёх камер — снимок и stat gpu 3. Снимок первого запуска — эталон кадра; пиксели,
которые расходятся между запусками одной сборки сверх допуска (рябь воды по времени игры, единичные пиксели расстановки,
TODO.md «Решить или проверить»), с запасом в --dilate пикселей — маска шума <камера>_noise.png: там расхождения не
считаются. Время GPU кадра — среднее по запускам. Всё — в reference\\<config>\\ (в .gitignore: кадры зависят от
Textures\\ и Meshes\\ этой машины) с reference.json: коммит, дата, время GPU.

check — один запуск текущей сборки с теми же камерами в reference\\<config>\\last\\: кадр совпал, если пикселей сверх
допуска ±1 вне маски шума не больше --allowed (карта расхождений — last\\<камера>_diff.png: красным — сверх допуска вне
маски, синим — маска шума); время GPU кадра — не больше эталона на --gpu-threshold %. Между отдельными запусками время
шумит (фоновая нагрузка GPU, docs/d3d12.md §4): медленнее порога — повторить, для сравнения двух сборок — попеременные
запуски. В Debug время GPU не сравнивается, зато debug-слой D3D12 должен молчать: строки «D3D12 error / warning /
corruption» в log.txt — провал (--gpu-validation — с GPU-based validation, для правок слоя D3D12).

Код выхода: 0 — всё совпало, 1 — расходятся кадры или есть сообщения debug-слоя, 3 — кадры совпали, но GPU медленнее
порога, 2 — проверка не прошла (движок не запустился или завис, нет эталона). Нужны numpy и Pillow. Сообщения скрипта —
ASCII (правило Tools/).
"""
import argparse
import datetime
import json
import os
import re
import subprocess
import sys

import numpy as np
from PIL import Image

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
ENGINE = os.path.join(TOOLS, 'engine.py')
sys.path.insert(0, TOOLS)
from compare_frames import load  # noqa: E402

# Контрольные камеры docs/d3d12.md §4: x,y,z,pitch,yaw
CAMERAS = [
    ('overview', '512,90,150,20,0'),
    ('balls', '500,20,215,4,5'),
    ('grass', '530,12.8,250,8,210'),
    ('grove', '480,12.6,320,3,0'),
]
TOLERANCE = 1     # ±1 — двоякое округление экспозиции между запусками (TODO.md, «Решить или проверить»)


def engine(args, timeout=None):
    """engine.py с аргументами: код выхода и вывод"""
    command = [sys.executable, ENGINE] + (['--timeout', str(timeout)] if timeout else []) + args
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, errors='replace')
    return result.returncode, result.stdout + result.stderr


def run_cameras(config, folder, gpu, gpu_validation):
    """Запуск движка, снимки камер в folder и время GPU кадра на каждой (gpu); None — движок не запустился или завис"""
    os.makedirs(folder, exist_ok=True)
    start = ['start', '--config', config, '--nogui', '--nowind', '--noparticles', '--freezetime', '--nocolorgrading']
    if gpu_validation:
        start.append('--gpu-validation')
    code, output = engine(start)
    if code != 0:
        print(output.rstrip())
        return None
    script = os.path.join(folder, 'cameras.txt')
    with open(script, 'w', encoding='utf-8') as f:
        for name, camera in CAMERAS:
            f.write('camera %s\n' % camera)
            f.write('screenshot "%s"\n' % os.path.join(folder, name + '.png'))
            if gpu:
                f.write('stat gpu 3\n')
    # Debug с GPU-based validation — кадр в разы дольше: и ответ дольше
    code, output = engine(['run', script], timeout=300 if config == 'Debug' else None)
    os.remove(script)
    times = {}
    camera = None
    for line in output.splitlines():
        if line.startswith('> camera '):
            camera = next((n for n, c in CAMERAS if c == line[len('> camera '):].strip()), None)
        match = re.search(r'GPU average over \d+ frames, ms: frame ([0-9.]+)', line)
        if match and camera:
            times[camera] = float(match.group(1))
    stop_code, stop_output = engine(['stop'])
    if code != 0:
        print(output.rstrip())
        print(stop_output.rstrip())
        return None
    return times


def debug_layer_messages():
    """Строки debug-слоя D3D12 в log.txt (DMD3D::messageCallback) и включён ли слой"""
    try:
        with open(os.path.join(ROOT, 'log.txt'), encoding='utf-8', errors='replace') as f:
            log = f.read().splitlines()
    except OSError:
        return False, []
    enabled = any('debug layer is enabled' in line for line in log)
    messages = [line for line in log if re.search(r'D3D12 (error|warning|corruption):', line)]
    return enabled, messages


def dilate(mask, radius):
    """Маска с запасом radius пикселей во все стороны"""
    height, width = mask.shape
    padded = np.pad(mask, radius)
    out = np.zeros_like(mask)
    for dy in range(2 * radius + 1):
        for dx in range(2 * radius + 1):
            out |= padded[dy:dy + height, dx:dx + width]
    return out


def beyond(a, b):
    """Пиксели, у которых хотя бы один канал расходится сверх допуска"""
    return np.abs(a - b).max(axis=2) > TOLERANCE


def git_state():
    def git(*args):
        result = subprocess.run(['git'] + list(args), cwd=ROOT, capture_output=True, text=True, errors='replace')
        return result.stdout.strip()
    commit = git('rev-parse', '--short', 'HEAD')
    # Без настроек запуска CLion (.run/: их меняет сама IDE) — они на кадр не влияют
    changed = [line[3:] for line in git('status', '--porcelain', '--untracked-files=no').splitlines()
               if not line[3:].startswith('.run/')]
    return commit, changed


def make_reference(args, folder):
    runs = []
    for run in range(args.runs):
        run_folder = os.path.join(folder, 'run%d' % run)
        print('reference run %d of %d (%s)...' % (run + 1, args.runs, args.config))
        sys.stdout.flush()
        times = run_cameras(args.config, run_folder, args.config == 'Release', args.gpu_validation)
        if times is None:
            print('error: the engine failed, no reference')
            return 2
        runs.append((run_folder, times))

    cameras = {}
    for name, camera in CAMERAS:
        frames = [load(os.path.join(run_folder, name + '.png')) for run_folder, _ in runs]
        noise = np.zeros(frames[0].shape[:2], dtype=bool)
        for frame in frames[1:]:
            if frame.shape != frames[0].shape:
                print('error: %s: frame size differs between runs' % name)
                return 2
            noise |= beyond(frames[0], frame)
        raw = int(noise.sum())
        noise = dilate(noise, args.dilate)
        Image.fromarray(frames[0].astype(np.uint8)).save(os.path.join(folder, name + '.png'))
        Image.fromarray((noise * 255).astype(np.uint8)).save(os.path.join(folder, name + '_noise.png'))
        times = [t[name] for _, t in runs if name in t]
        cameras[name] = {'camera': camera, 'gpu_frame_ms': round(sum(times) / len(times), 3) if times else None,
                         'gpu_frame_ms_runs': times, 'noise_pixels': raw, 'noise_mask_pixels': int(noise.sum())}
        print('%-9s noise between runs: %d px (mask %d px)%s' % (
            name, raw, int(noise.sum()),
            ', GPU frame %.3f ms (%s)' % (cameras[name]['gpu_frame_ms'], ', '.join('%.3f' % t for t in times)) if times else ''))
    for run_folder, _ in runs:
        for name, _ in CAMERAS:
            os.remove(os.path.join(run_folder, name + '.png'))
        os.rmdir(run_folder)

    commit, changed = git_state()
    info = {'config': args.config, 'date': datetime.datetime.now().strftime('%Y-%m-%d %H:%M'), 'commit': commit,
            'changed_files': changed, 'runs': args.runs, 'tolerance': TOLERANCE, 'dilate': args.dilate, 'cameras': cameras}
    with open(os.path.join(folder, 'reference.json'), 'w', encoding='utf-8') as f:
        json.dump(info, f, indent=2)
    print('reference written to %s (commit %s%s)' % (folder, commit, ', with uncommitted changes' if changed else ''))
    if args.config == 'Debug':
        return report_debug_layer()
    return 0


def report_debug_layer():
    enabled, messages = debug_layer_messages()
    if not enabled:
        print('debug layer: FAILED, no "debug layer is enabled" in log.txt')
        return 1
    if messages:
        print('debug layer: FAILED, %d messages in log.txt:' % len(messages))
        for line in messages[:10]:
            print('  ' + line)
        return 1
    print('debug layer: clean')
    return 0


def check(args, folder):
    try:
        with open(os.path.join(folder, 'reference.json'), encoding='utf-8') as f:
            info = json.load(f)
    except OSError:
        print('error: no reference in %s: python Tools/check_frames.py reference --config %s' % (folder, args.config))
        return 2
    print('reference: %s, commit %s%s, %s, %d runs' % (info['config'], info['commit'],
                                                       ' (with uncommitted changes)' if info.get('changed_files') else '',
                                                       info['date'], info['runs']))
    last = os.path.join(folder, 'last')
    gpu = args.config == 'Release'
    times = run_cameras(args.config, last, gpu, args.gpu_validation)
    if times is None:
        print('error: the engine failed')
        return 2

    frames_ok = True
    slower = []
    for name, _ in CAMERAS:
        reference = load(os.path.join(folder, name + '.png'))
        current = load(os.path.join(last, name + '.png'))
        if reference.shape != current.shape:
            print('%-9s FRAME SIZE DIFFERS: %dx%d vs reference %dx%d' % (name, current.shape[1], current.shape[0],
                                                                        reference.shape[1], reference.shape[0]))
            frames_ok = False
            continue
        noise = np.asarray(Image.open(os.path.join(folder, name + '_noise.png'))) > 0
        differs = beyond(reference, current)
        outside = differs & ~noise
        count = int(outside.sum())
        max_diff = int(np.abs(reference - current).max(axis=2)[outside].max()) if count else 0
        ok = count <= args.allowed
        frames_ok = frames_ok and ok
        line = '%-9s frame %s: %d px beyond +-%d outside the noise mask%s (%d inside)' % (
            name, 'ok' if ok else 'DIFFERS', count, TOLERANCE, ', max diff %d' % max_diff if count else '',
            int((differs & noise).sum()))
        diff_path = os.path.join(last, name + '_diff.png')
        if count:
            gray = (reference.mean(axis=2) * 0.5).astype(np.uint8)
            image = np.stack([gray, gray, gray], axis=2)
            image[noise, 2] = np.maximum(image[noise, 2], 96)
            strength = np.clip(np.abs(reference - current).max(axis=2)[outside] * 4 + 96, 0, 255).astype(np.uint8)
            image[outside] = 0
            image[outside, 0] = strength
            Image.fromarray(image).save(diff_path)
        elif os.path.exists(diff_path):
            os.remove(diff_path)
        reference_time = info['cameras'].get(name, {}).get('gpu_frame_ms')
        if gpu and name in times and reference_time:
            change = 100.0 * (times[name] - reference_time) / reference_time
            slow = change > args.gpu_threshold
            if slow:
                slower.append(name)
            line += ' | GPU frame %.3f ms vs %.3f (%+.1f%%)%s' % (times[name], reference_time, change, ' SLOWER' if slow else '')
        print(line)

    result = 0 if frames_ok else 1
    if args.config == 'Debug':
        if report_debug_layer() != 0:
            result = 1
    if result == 0 and slower:
        print('GPU slower than the reference by more than %.0f%% at: %s. GPU time between runs is noisy (docs/d3d12.md '
              '4): run the check again; to compare two builds, run them in turn' % (args.gpu_threshold, ', '.join(slower)))
        result = 3
    print('result: %s' % {0: 'OK', 1: 'FAILED', 3: 'frames OK, GPU slower'}[result])
    if not frames_ok:
        print('difference maps: %s\\<camera>_diff.png (red - beyond tolerance, blue - noise mask)' % last)
    return result


def main():
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(errors='replace')
    argv = sys.argv[1:]
    mode = 'check'
    if argv and argv[0] in ('reference', 'check'):
        mode = argv.pop(0)
    parser = argparse.ArgumentParser(prog='check_frames.py ' + mode,
                                     description='Control cameras against the reference: frames and GPU average')
    parser.add_argument('--config', choices=['Debug', 'Release'], default='Release')
    parser.add_argument('--gpu-validation', action='store_true',
                        help='Debug: GPU-based validation of the D3D12 debug layer (for changes of the D3D12 layer)')
    parser.add_argument('--runs', type=int, default=2, help='reference: engine runs; the noise mask is what differs between them')
    parser.add_argument('--dilate', type=int, default=2, help='reference: noise mask margin, pixels')
    parser.add_argument('--allowed', type=int, default=32,
                        help='check: pixels beyond tolerance outside the noise mask that still pass')
    parser.add_argument('--gpu-threshold', type=float, default=10.0, help='check: GPU frame slower than the reference, %%')
    args = parser.parse_args(argv)
    if args.gpu_validation and args.config != 'Debug':
        raise SystemExit('error: --gpu-validation needs --config Debug')
    if args.runs < 2 and mode == 'reference':
        raise SystemExit('error: the reference needs at least 2 runs (the noise mask)')
    folder = os.path.join(ROOT, 'reference', args.config.lower())
    os.makedirs(folder, exist_ok=True)
    return make_reference(args, folder) if mode == 'reference' else check(args, folder)


if __name__ == '__main__':
    sys.exit(main())
