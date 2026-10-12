"""Разбор захвата PIX (команда pix движка, docs/d3d12.md §4): дерево областей PIXBeginEvent с числом вызовов, dispatch и
барьеров — как вкладка Events в PIX, но текстом, для сравнения захватов до и после правки.

    python Tools/pix_events.py захват.wpix [--csv events.csv]     (список событий выгружает pixtool из установленного PIX)
    python Tools/pix_events.py events.csv                          (уже выгруженный: pixtool save-event-list)

На строку области — вызовы (Draw*, ExecuteIndirect), dispatch и события Barrier (одно событие — группа барьеров). Барьер
посреди работы области (после её первого вызова) — «interleaved»: проход объявляет цели и чтения в PassDesc, и
DMD3D::beginPass ставит их барьеры пачкой в начале; барьер посреди прохода — привязка вида, который пишет текущий проход
(setSRV завершает проход над ним), или зависимость dispatch от dispatch (UAV). Время событий (счётчики TOP to EOP)
pixtool отдаёт только с правами на сбор данных производительности — PIX от администратора или группа Performance Log
Users; без них печатается только структура.

Сообщения скрипта — ASCII (правило Tools/).
"""
import argparse
import csv
import glob
import os
import subprocess
import sys
import tempfile


def find_pixtool():
    """pixtool.exe последней установленной версии PIX."""
    candidates = sorted(glob.glob(os.path.join(os.environ.get('ProgramFiles', r'C:\Program Files'), 'Microsoft PIX', '*',
                                               'pixtool.exe')))
    if not candidates:
        raise SystemExit('error: PIX is not installed (no Microsoft PIX\\*\\pixtool.exe)')
    return candidates[-1]


def export_events(capture, csv_path):
    command = [find_pixtool(), '--output=quiet', 'open-capture', os.path.abspath(capture), 'save-event-list',
               os.path.abspath(csv_path)]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0 or not os.path.exists(csv_path):
        raise SystemExit('error: pixtool failed:\n' + result.stdout + result.stderr)


def is_work(name):
    return name.startswith('Draw') or name.startswith('Dispatch') or name == 'ExecuteIndirect'


def main():
    parser = argparse.ArgumentParser(description='PIX capture event tree: markers, draws, dispatches and barriers')
    parser.add_argument('source', help='.wpix capture or event list .csv (pixtool save-event-list)')
    parser.add_argument('--csv', help='where to keep the event list exported from a .wpix')
    args = parser.parse_args()

    csv_path = args.source
    if args.source.lower().endswith('.wpix'):
        csv_path = args.csv or os.path.join(tempfile.gettempdir(), 'pix_events.csv')
        export_events(args.source, csv_path)

    with open(csv_path, encoding='utf-8-sig') as f:
        rows = list(csv.reader(f, skipinitialspace=True))
    events = {}
    children = {}
    for row in rows[1:]:
        if len(row) < 3:
            continue
        index, parent, name = int(row[0]), int(row[1]), row[2]
        events[index] = (parent, name)
        children.setdefault(parent, []).append(index)

    # Область — событие с детьми, кроме ExecuteIndirect (PIX раскрывает его в вызовы)
    def is_marker(index):
        return index in children and events[index][1] != 'ExecuteIndirect'

    totals = {'draws': 0, 'dispatches': 0, 'barriers': 0}
    interleaved = []

    def visit(index, depth):
        kids = children.get(index, [])
        draws = dispatches = barriers = 0
        seen_work = False
        late_barriers = 0
        for kid in kids:
            name = events[kid][1]
            if name == 'Barrier':
                barriers += 1
                late_barriers += 1 if seen_work else 0
            elif name.startswith('Dispatch'):
                dispatches += 1
                seen_work = True
            elif is_work(name):
                draws += 1 if name != 'ExecuteIndirect' else len(children.get(kid, [])) or 1
                seen_work = True
        if index >= 0:
            name = events[index][1]
            note = '  interleaved %d' % late_barriers if late_barriers else ''
            print('%s%-*s draws %4d  dispatch %3d  barriers %3d%s' % ('  ' * depth, 40 - 2 * depth, name, draws, dispatches,
                                                                     barriers, note))
            if late_barriers:
                interleaved.append((name, late_barriers))
        totals['draws'] += draws
        totals['dispatches'] += dispatches
        totals['barriers'] += barriers
        for kid in kids:
            if is_marker(kid):
                visit(kid, depth + 1 if index >= 0 else 0)

    visit(-1, 0)
    print('Events %d: draws %d, dispatches %d, barrier events %d' % (len(events), totals['draws'], totals['dispatches'],
                                                                     totals['barriers']))
    if interleaved:
        print('Barriers after the first draw or dispatch of a marker: ' +
              ', '.join('%s %d' % (name, count) for name, count in interleaved))


if __name__ == '__main__':
    main()
