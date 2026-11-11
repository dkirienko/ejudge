# Downstream-политика ejudge

Дата: 2026-11-11

## Модель ветвей

- `upstream/*` — только зеркало blackav/ejudge. Локальные коммиты не добавлять.
- `sch179/next` — интеграционная ветка. Собирается из upstream-тега и `topic/*`; здесь выполняются сборка, тесты и релизные теги.
- `topic/*` — одна тема на ветку. Все изменения сначала попадают сюда.
- `v3.14-sch179` — legacy-ветка, заморожена. Новые изменения в неё не вносить.

## Текущие тематические ветки

| Ветка | Коммиты | Назначение | Статус |
|---|---|---|---|
| `topic/school-179` | `98dea8aff`, `e48f5f4eb`, `6828f77b6`, `2d4576762` | четыре патча школы 179 | local |
| `topic/daily-statistics` | `b6131c57c` | исправление обхода посылок в daily statistics | pending |
| `topic/ok-displayed-runs` | `ddf98b0f4`, `9362e34c8` | действие OK displayed runs + стабильные ID действий | local |
| `topic/problem-dir-variants` | `ff28ecf4f` | несколько `problem_dir` для вариантных задач | pending |
| `topic/fastejudge` | `044688a84` | встроенный fastEjudge с Prism | local/canonical |
| `topic/downstream-docs` | см. ветку | политика downstream и TODO | local |

Интеграционная ветка `sch179/next` собрана из `v3.14.0`; её дерево совпадает с `v3.14-sch179`.

## Происхождение патчей

Для импортированных патчей сохранять трейлеры:

```text
Origin: ShashkovS/ejudge branch new_problem_dir_logic_and_ui
Origin-Commit: 8232887e4
Upstream-Status: pending | accepted | local | obsolete
```

Для fastEjudge:

```text
Canonical: dkirienko/ejudge style/fastejudge.* (primary; browser extension archived)
Upstream-Status: local
```

При обновлении upstream проверять, не принят ли патч:

```bash
git log --cherry-pick upstream/master..sch179/next
git cherry upstream/master sch179/next
git range-diff upstream/master...sch179/next
```

## Порядок работы

1. Изменения делать в соответствующей `topic/*` ветке.
2. Проверять сборку/тесты в этой ветке, если возможно.
3. Вливать в `sch179/next` через `git merge --no-ff`.
4. Для релиза ставить тег на `sch179/next`, например `v3.15-sch179-1`.
5. Деплой выполнять строго по тегу.

## Обновление upstream

```bash
git fetch --all --tags
git switch -c sch179/3.15 upstream/3.15
git merge topic/school-179 topic/daily-statistics topic/ok-displayed-runs \
          topic/problem-dir-variants topic/fastejudge
make && тесты
git tag v3.15-sch179-1
```

Если патч уже принят upstream — удалить соответствующую тему из merge-набора.

## Правила для fastEjudge

- Канонический источник: `style/fastejudge.js`, `style/fastejudge.css` в этом репозитории.
- Браузерное расширение архивируется; правки в него не вносить.
- Развитие — через `topic/fastejudge`; подробности в `FASTEJUDGE-TODO.md`.
- В перспективе разделить ядро UI и адаптер ejudge.

## Уборка

- Не хранить `0001–0004*.patch` как источник истины: они уже представлены коммитами.
- Слитые и устаревшие ветки удалять после проверки.
- Опубликованные теги не переписывать.
- Не коммитить autotools-шум (`configure`, `libbacktrace/*`, `libdwarf/*`).
