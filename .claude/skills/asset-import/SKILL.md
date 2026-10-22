---
name: asset-import
description: Добавить или пересобрать данные DMEngine не из кода — модель или растение из Blender / Poly Haven / Megascans через glTF, слой материала террейна из фото-текстур, запись звука, карту высот, дальние горы — и залить их на NAS. Использовать, когда появился новый ассет, архив в DownloadResources или нужно восстановить ресурсы на машине.
---

# Импорт ассетов

Ассеты — строки `base.db3` и файлы в `Textures\`, `Meshes\`, `Sounds\` (не в git); исходники — `DownloadResources\`.
Скачивание с сайтов — только с разрешения Dimok (Fab / Megascans качает он сам: нужен вход). Blender 5.0 —
`C:\Program Files\Blender Foundation\Blender 5.0\blender.exe` (не в PATH), запуск без окна: `blender -b --factory-startup
--python Tools/<сценарий>.py -- …`.

- **Модели и растения** — `docs/models.md`: экспорт glTF (сценарии `Tools/blender_*.py`, `Tools/export_polyhaven.py`,
  `Tools/export_megascans.py` — архив FBX Megascans или загрузка лаунчера Fab, в том числе новый формат glTF; Poly Haven — `python Tools/polyhaven_download.py <ассет> --resolution 2k`), затем
  `python Tools/import_gltf.py файл.glb` — модель уровня (`--level ValleyOfStreams --position x,y,z [--snap-to-terrain]`) или
  расстановки (`--scatter`); LOD — `--lod-ranges`, тени — `--shadow-min-lod`, `--shadow-lod-scale`; параметры движка,
  которых нет в glTF (ветер, пропускание, смена LOD, нормаль кроны), — `--material-param Имя=значение` или extras в
  сценарии. Повторный импорт обновляет строки.
- **Слои террейна** — `docs/terrain.md`: `Tools/pack_terrain_layer.py` через Blender (`--layer 0…11`), строки
  `TerrainLayers`, роли слоёв; процедурные тестовые — `Tools/gen_terrain_textures.py`.
- **Рельеф** — исходная карта `python Tools/gen_heightmap.py` (эрозию, русла и маски движок считает сам при загрузке,
  итог — `Textures\terrain\eroded\`), дальние горы — `python Tools/gen_far_terrain.py` (`docs/far_terrain.md`).
- **Звук** — `docs/audio.md`: `python Tools/pack_sound.py DownloadResources/Sounds/<файл> --name имя --author … --license …
  --source-url …` (фразы — порог `--threshold`, петля — `--whole`, фильтр — `--highpass`); слой певцов —
  `Tools/soundscape.py add` и `rule-add`; звуки окружения — `Tools/ambient_sound.py set`. У записей CC BY автор
  обязателен (строка `Sounds` и таблица в `docs/audio.md`). Без наушников — `audio record` и `Tools/audio_check.py`.
- **NAS** — после новых архивов, текстур, мешей, записей: `python Tools/nas_sync.py status`, затем `push`; на новой машине
  — `pull`. Запускать из PowerShell (из песочницы Git Bash SSH в локальную сеть не ходит).

После импорта — запуск: в `log.txt` нет заглушек нового ассета; снимок с ним; закрыть пункт — скил `finish-item`.
