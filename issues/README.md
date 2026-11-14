# Local issue tracker

This repo uses plain markdown files as its issue tracker. There is no git remote or hosted tracker yet.

## Issue files

Each issue is one file: `issues/NNNN-<slug>.md`. The four-digit number is the issue's id and never changes. The file starts with frontmatter:

```yaml
---
id: 2
title: Human-readable title (the issue's name; refer to it by this)
labels: [wayfinder:research]
status: open            # open | closed
assignee:               # empty = unclaimed
parent: 1               # id of the parent issue (e.g. the wayfinder map)
blocked_by: [5, 6]      # ids of issues that must close first
---
```

## Wayfinding operations

- **Map**: the issue labelled `wayfinder:map`.
- **Child tickets**: issues whose `parent` is the map's id.
- **Blocking**: the `blocked_by` list. There's no native dependency UI, so this frontmatter field is the convention.
- **Claim**: set `assignee` before doing any work.
- **Frontier**: children where `status: open`, `assignee` is empty, and every id in `blocked_by` is `status: closed`.
- **Resolve**: append a `## Resolution` section (the resolution comment), set `status: closed`, then add a line to the map's *Decisions so far*.
- **Assets**: research findings live in `research/<name>.md`, and other assets live elsewhere in the repo. Tickets link to them instead of pasting them in.
