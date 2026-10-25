# Contributing to exteraDesktop

## Before you open a PR

- Keep PRs scoped to one thing. A fix, a feature, a CI change — not all three at once.
- If you're fixing a build/platform bug, say what broke and why in the PR description, not just what changed.
- Test what you can before opening the PR. If you built and ran it locally, say so (and on what OS/arch). If you couldn't test something (no access to that OS, no hardware), say that too instead of leaving it implicit.
- Draft PRs are fine for work in progress — mark them as draft rather than opening early for review.

## Code style

This fork follows the existing style already used throughout the codebase (it's inherited from upstream [tdesktop](https://github.com/telegramdesktop/tdesktop)):

- Tabs for indentation.
- Braces on the same line (`if (...) {`).
- `camelCase` for variables and functions, `PascalCase` for types.
- Match the surrounding file's style over any abstract preference — this is a large, old codebase and consistency within a file matters more than any single rule.

There's no `.clang-format` config and no plan to add one that auto-reformats the whole tree — that would produce a diff nobody could review. If you want an editor to help, configure it manually (tabs, same-line braces) rather than relying on a formatter.

## Local lint/format on commit

There's a `.pre-commit-config.yaml` covering the same things CI checks, plus `shfmt` to format the build shell scripts. Install it once:

```
pip install pre-commit
pre-commit install
```

After that it runs automatically on `git commit`, only on the files you're touching. Run it on everything manually with `pre-commit run --all-files`.

It's scoped to `.github/` and `Telegram/build/prepare/` — it does not touch the C++ codebase, for the same reason there's no `.clang-format`: a repo-wide auto-format would be an unreviewable diff.

## CI checks

Every PR runs `.github/workflows/pr-checks.yml`:

- `actionlint` on any workflow files you touched.
- A Python syntax check on `Telegram/build/prepare/prepare.py`.
- `shellcheck` (error severity only) on the `.sh` build scripts.

These are meant to catch real mistakes, not enforce style. If a check fails, the log tells you exactly what and where.

Platform build workflows (`macos-build.yml`, `windows-build.yml`) only run on release publish or manual dispatch — they're not part of per-PR checks since a full build takes hours.

## Build scripts (`prepare.py` and friends)

If you're touching `Telegram/build/prepare/`, keep in mind it's a single script shared across win/mac/linux, gated by tags (`win:`, `mac:`, `!win:`, `common:`, etc.) inside the multi-line stage strings. A command with no tag before it runs on *every* platform — that's the most common way to accidentally break a platform you're not testing on. When in doubt, check `filterByPlatform()` in `prepare.py` to see how the tag you're adding actually resolves.
