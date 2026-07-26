# Contributing

## Workflow

Create `feature/<kebab-title>`, `fix/<kebab-title>`, or `chore/<kebab-title>` from an up-to-date `develop`. Open a pull request to `develop`; release pull requests merge `develop` into `main`. `develop` and `main` are protected: direct pushes, force-pushes, and deletion are not allowed.

Use Conventional Commit subjects (`feat:`, `fix:`, `docs:`, `chore:`, `refactor:`, `test:`, `build:`, or `ci:`). Explain non-trivial work in the commit body. Keep commits focused and rebase to preserve linear history.

## Before opening a pull request

Run formatting and the macOS CI-equivalent build. Manually exercise affected OBS sources and verify Accessibility permission behavior for capture changes. Complete every section of the pull request template, including a release-note entry in `CHANGELOG.md` when user-visible behavior changes.

## Releases

Stable versions are SemVer values in `buildspec.json`, changed by the release PR. Use the repository skills for branch/PR/release procedures; do not put signing credentials in the repository.

Repository administrators can reconcile branch rules with `./scripts/configure-github-rulesets.sh`. It configures active rulesets for `develop` and `main` with pull requests, resolved conversations, linear history, formatting/build/CodeQL checks, and blocked force-pushes/deletions; it intentionally requires zero approvals for the sole maintainer.
