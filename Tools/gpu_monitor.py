"""Состояние GPU во время замеров (Tools/check_frames.py): частота, мощность, температура и ограничения частоты — nvidia-smi
два раза в секунду; чужие процессы на том же GPU — счётчики Windows «GPU Engine» раз в секунду (как столбец «GPU» в
диспетчере задач). Так медленный замер сразу показывает причину: упала частота (питание ноутбука, нагрев) или GPU делит
время с другой программой (тогда частота нормальная, а отметки времени движка включают чужую работу).

    monitor = GpuMonitor(engine_pid)
    monitor.start()
    ... замер от t0 до t1 (time.time()) ...
    state = monitor.state(t0, t1)       # словарь: clock_mhz, power_w, ..., others
    print(describe(state))
    monitor.stop()

Без nvidia-smi (не NVIDIA) — только чужие процессы; без PowerShell — только nvidia-smi. Сообщения — ASCII (правило Tools/).
"""
import csv
import io
import re
import subprocess
import threading
import time

# Биты clocks_event_reasons.active (nvidia-smi --help-query-gpu): почему частота ниже наибольшей
REASONS = [
    (0x2, 'application clocks'),
    (0x4, 'power cap'),
    (0x8, 'HW slowdown'),
    (0x10, 'sync boost'),
    (0x20, 'SW thermal'),
    (0x40, 'HW thermal'),
    (0x80, 'HW power brake'),
    (0x100, 'display clock'),
]

NVIDIA_QUERY = ('clocks.gr,clocks.mem,power.draw,temperature.gpu,utilization.gpu,clocks_event_reasons.active,pstate,'
                'enforced.power.limit')

# Загрузка движков GPU по процессам: строка на опрос — «pid luid engtype=процент ...» (только > 0,1 %)
POWERSHELL_LOOP = r"""
while ($true) {
  $samples = (Get-Counter '\GPU Engine(*)\Utilization Percentage' -ErrorAction SilentlyContinue).CounterSamples
  $items = foreach ($s in $samples) {
    if ($s.CookedValue -gt 0.1 -and $s.InstanceName -match 'pid_(\d+)_luid_(0x[0-9a-f]+_0x[0-9a-f]+)_.*engtype_(\w+)') {
      '{0}:{1}:{2}={3:F1}' -f $matches[1], $matches[2], $matches[3], $s.CookedValue
    }
  }
  [Console]::Out.WriteLine('sample ' + ($items -join ' '))
  [Console]::Out.Flush()
}
"""


class GpuMonitor:
    def __init__(self, engine_pid=None):
        self.engine_pid = engine_pid
        # (время, частота МГц, память МГц, Вт, °C, загрузка %, биты ограничений, P-state, предел мощности Вт)
        self.nvidia = []
        self.processes = []   # (время, {pid: {luid: {engtype: %}}})
        self.lock = threading.Lock()
        self.children = []
        self.threads = []

    def start(self):
        self._spawn(['nvidia-smi', '--query-gpu=' + NVIDIA_QUERY, '--format=csv,noheader,nounits', '-lms', '500'],
                    self._read_nvidia)
        self._spawn(['powershell', '-NoProfile', '-NonInteractive', '-Command', POWERSHELL_LOOP], self._read_processes)

    def _spawn(self, command, reader):
        try:
            child = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, errors='replace',
                                     creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        except OSError:
            return
        self.children.append(child)
        thread = threading.Thread(target=reader, args=(child,), daemon=True)
        thread.start()
        self.threads.append(thread)

    def _read_nvidia(self, child):
        for line in child.stdout:
            parts = [p.strip() for p in line.split(',')]
            if len(parts) != 8:
                continue
            try:
                values = [float(p) for p in parts[:5]] + [int(parts[5], 16), int(parts[6].lstrip('P')), float(parts[7])]
            except ValueError:
                continue
            with self.lock:
                self.nvidia.append((time.time(),) + tuple(values))

    def _read_processes(self, child):
        for line in child.stdout:
            if not line.startswith('sample'):
                continue
            usage = {}
            for item in line.split()[1:]:
                match = re.match(r'(\d+):([^:]+):(\w+)=([0-9.,]+)$', item)
                if match:
                    pid, luid, engine, value = int(match.group(1)), match.group(2), match.group(3), float(match.group(4).replace(',', '.'))
                    engines = usage.setdefault(pid, {}).setdefault(luid, {})
                    engines[engine] = engines.get(engine, 0.0) + value
            with self.lock:
                self.processes.append((time.time(), usage))

    def stop(self):
        for child in self.children:
            try:
                child.kill()
            except OSError:
                pass
        for child in self.children:
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        self.children = []

    def state(self, begin, end):
        """Состояние GPU за отрезок [begin, end] по time.time(); None — ни одного опроса"""
        with self.lock:
            nvidia = [s for s in self.nvidia if begin <= s[0] <= end]
            # Опрос счётчиков длится ~1 с и помечен концом: берём и тот, что закончился чуть позже отрезка
            processes = [s for s in self.processes if begin < s[0] <= end + 1.0]
        if not nvidia and not processes:
            return None
        state = {'samples': len(nvidia)}
        if nvidia:
            clocks = sorted(s[1] for s in nvidia)
            state['clock_mhz'] = clocks[len(clocks) // 2]
            state['clock_min_mhz'] = clocks[0]
            state['memory_clock_mhz'] = sorted(s[2] for s in nvidia)[len(nvidia) // 2]
            state['power_w'] = round(sum(s[3] for s in nvidia) / len(nvidia), 1)
            state['temperature_c'] = max(s[4] for s in nvidia)
            state['utilization'] = round(sum(s[5] for s in nvidia) / len(nvidia))
            state['pstate'] = max(s[7] for s in nvidia)
            state['power_limit_w'] = min(s[8] for s in nvidia)
            state['limits'] = {name: round(sum(1 for s in nvidia if s[6] & bit) / len(nvidia), 2)
                               for bit, name in REASONS if any(s[6] & bit for s in nvidia)}
        if processes:
            # GPU движка — адаптеры (luid), на которых работает его процесс; чужие — на тех же адаптерах
            luids = {luid for _, usage in processes for luid in usage.get(self.engine_pid, {})}
            totals = {}
            for _, usage in processes:
                for pid, adapters in usage.items():
                    if pid == self.engine_pid:
                        continue
                    busiest = max((value for luid, engines in adapters.items() if not luids or luid in luids
                                   for value in engines.values()), default=0.0)
                    totals[pid] = totals.get(pid, 0.0) + busiest
            names = process_names()
            others = sorted(((names.get(pid, 'pid %d' % pid), round(total / len(processes), 1)) for pid, total in totals.items()),
                            key=lambda item: -item[1])
            state['others'] = [[name, value] for name, value in others if value >= 1.0][:4]
        return state


def process_names():
    """pid → имя процесса (tasklist)"""
    try:
        output = subprocess.run(['tasklist', '/FO', 'CSV', '/NH'], capture_output=True, text=True, errors='replace').stdout
    except OSError:
        return {}
    names = {}
    for row in csv.reader(io.StringIO(output)):
        if len(row) >= 2 and row[1].isdigit():
            names[int(row[1])] = row[0][:-4] if row[0].lower().endswith('.exe') else row[0]
    return names


def describe(state, reference=None):
    """Строка о состоянии GPU; reference — состояние эталона: частота рядом с его частотой"""
    if not state:
        return 'GPU state: no samples'
    parts = ['%.0f frames/s' % state['fps']] if 'fps' in state else []
    if 'clock_mhz' in state:
        clock = '%.0f MHz' % state['clock_mhz']
        if state['clock_min_mhz'] < state['clock_mhz'] * 0.9:
            clock += ' (min %.0f)' % state['clock_min_mhz']
        if reference and reference.get('clock_mhz'):
            clock += ' vs %.0f in the reference' % reference['clock_mhz']
        parts.append('%s, P%d, %.0f of %.0f W, %.0f C, load %d%%' % (clock, state['pstate'], state['power_w'], state['power_limit_w'],
                                                                     state['temperature_c'], state['utilization']))
        limits = ', '.join('%s%s' % (name, '' if share >= 0.99 else ' %d%%' % round(share * 100))
                           for name, share in state['limits'].items())
        parts.append('limits: ' + (limits or 'none'))
    if 'others' in state:
        parts.append('other processes on the GPU: ' + (', '.join('%s %.1f%%' % (n, v) for n, v in state['others']) or 'none'))
    return 'GPU state: ' + '; '.join(parts)
