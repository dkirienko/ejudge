# Схема репозитория ejudge

Дата: 2026-11-11

## 1. Удалённые репозитории

| Remote | Назначение | Политика |
|---|---|---|
| `upstream` | blackav/ejudge | Только чтение, источник upstream-тегов |
| `shashkovs` | ShashkovS/ejudge | Источник патчей Шашкова |
| `origin` | dkirienko/ejudge | Основной публичный репозиторий |

Локальные изменения не отправляются в `upstream` и `shashkovs`. Публикация — только в `origin`.

## 2. Слои веток

```text
upstream/*  (чистый upstream, без локальных коммитов)
   │
   ├── topic/school-179
   ├── topic/daily-statistics
   ├── topic/ok-displayed-runs
   ├── topic/problem-dir-variants
   ├── topic/fastejudge
   └── topic/downstream-docs
             │
             ▼
       sch179/next  (интеграционная ветка)
             │
             ▼
       vX.Y-sch179-N  (релизные теги)
```

Старая ветка `v3.14-sch179` заморожена и используется как legacy/точка отката.

## 3. Текущие ветки

### Интеграция и темы

| Ветка | HEAD | Назначение |
|---|---|---|
| `sch179/next` | `36cbb3aeb` | Интеграционная ветка |
| `topic/school-179` | `2d4576762` | Патчи школы 179 |
| `topic/daily-statistics` | `b6131c57c` | Исправление daily statistics |
| `topic/ok-displayed-runs` | `9362e34c8` | OK displayed runs и стабильные ID действий |
| `topic/problem-dir-variants` | `ff28ecf4f` | Несколько `problem_dir` для вариантных задач |
| `topic/fastejudge` | `a19edb043` | Встроенный fastEjudge |
| `topic/downstream-docs` | `812911a2e` | Документация downstream-процесса |

### Legacy и служебные

| Ветка | HEAD | Статус |
|---|---|---|
| `v3.14-sch179` | `a3419a372` | Заморожена |
| `v3.14-sch179-test-shashkov` | `22d9196ef` | Ветка для проверки патча Шашкова |
| `v3.13-sch179` | `cea20a19d` | Прошлый релиз |
| `fix-daily-statistics-clean` | `dcebd5d22` | Слита |
| `fix-daily-statistics-rcur` | `1be972ba1` | Слита |
| `fix-daily-statistics-upstream` | `1be972ba1` | Служебная |
| `ok-displayed-runs` | `4535fafa4` | Слита |
| `mime-opendocument` | `c064caa67` | Слита |
| `master` | `03880f426` | Зеркало upstream master |

Все ветки и теги опубликованы в `origin`.

## 4. Состав патчей

| Патч | Ветка | Коммиты | Происхождение | Статус |
|---|---|---|---|---|
| Патчи школы 179 | `topic/school-179` | `98dea8aff`, `e48f5f4eb`, `6828f77b6`, `2d4576762` | локальные | local |
| Daily statistics | `topic/daily-statistics` | `b6131c57c` | локальный/перенесённый | pending |
| OK displayed runs | `topic/ok-displayed-runs` | `ddf98b0f4`, `9362e34c8` | локальные | local |
| `problem_dir` для вариантов | `topic/problem-dir-variants` | `ff28ecf4f` | ShashkovS, `8232887e4` | pending |
| fastEjudge | `topic/fastejudge` | `044688a84`, `d3e4a854f`, `a19edb043` | локальный порт + доработки | local/canonical |
| Downstream-документация | `topic/downstream-docs` | `b6652d430`, `812911a2e` | локальная | local |

Для импортированных патчей используются трейлеры:

```text
Origin: ShashkovS/ejudge branch new_problem_dir_logic_and_ui
Origin-Commit: 8232887e4
Upstream-Status: pending | accepted | local | obsolete
```

## 5. Рабочий процесс

1. Изменения делаются в соответствующей ветке `topic/*`.
2. Проверяются сборка и тесты.
3. Ветка вливается в `sch179/next`:

```bash
git switch sch179/next
git merge --no-ff topic/<name>
```

4. Для релиза ставится тег на `sch179/next`:

```bash
git tag v3.14-sch179-3
```

5. Публикация:

```bash
git push origin --all
git push origin --tags
```

6. Деплой выполняется строго по тегу.

## 6. Обновление upstream

```bash
git fetch --all --tags
git switch -c sch179/3.15 upstream/3.15
git merge topic/school-179 topic/daily-statistics topic/ok-displayed-runs \
          topic/problem-dir-variants topic/fastejudge
make && тесты
git tag v3.15-sch179-1
```

Перед переносом патчей проверить, не приняты ли они в upstream:

```bash
git log --cherry-pick upstream/master..sch179/next
git cherry upstream/master sch179/next
git range-diff upstream/master...sch179/next
```

Принятые патчи после проверки удаляются из локального набора.

## 7. fastEjudge

- Канонический источник: `style/fastejudge.js` и `style/fastejudge.css` в `origin/dkirienko/ejudge`.
- Браузерное расширение архивируется; двусторонняя поддержка не ведётся.
- Все изменения идут через `topic/fastejudge`.
- Текущие возможности: просмотр исходника, diff с предыдущей задачей, подсветка Prism, переключатель `Source`/`Diff`, длинное название задачи в панели решения, комментарии и быстрые действия.
- Подробный план доработок: `FASTEJUDGE-TODO.md`.

## 8. Правила гигиены

- Не коммитить autotools-шум: `configure`, `libbacktrace/*`, `libdwarf/*`.
- Файлы `0001–0004*.patch` не являются источником истины: соответствующие изменения уже есть в коммитах.
- Слитые и устаревшие ветки удалять после проверки.
- Опубликованные теги не переписывать.
- Секреты и локальные пароли в репозиторий не добавлять.

## 9. Текущее состояние

- `topic/fastejudge` содержит последний коммит `a19edb043` (длинное название задачи), который ещё не влит в `sch179/next` (`36cbb3aeb`).
- Следующий шаг: влить `topic/fastejudge` в `sch179/next`, проверить сборку и при необходимости поставить новый тег.
- Все существующие ветки и теги уже отправлены в `origin`.
