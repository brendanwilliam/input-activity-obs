# Contributing

## Workflow

Create `feature/<kebab-title>`, `fix/<kebab-title>`, or `chore/<kebab-title>` from an up-to-date `main`. Open a pull request to `main`. `main` is protected: direct pushes, force-pushes, and deletion are not allowed.

Use Conventional Commit subjects (`feat:`, `fix:`, `docs:`, `chore:`, `refactor:`, `test:`, `build:`, or `ci:`). Explain non-trivial work in the commit body. Keep commits focused and rebase to preserve linear history.

## Before opening a pull request

Run formatting and the macOS CI-equivalent build. Manually exercise affected OBS sources and verify Accessibility permission behavior for capture changes. Complete every section of the pull request template, including a release-note entry in `CHANGELOG.md` when user-visible behavior changes.

## Releases

Stable versions are SemVer values in `buildspec.json`, changed through a pull request to `main`. Validate release candidates from `main`, then create the matching stable tag on the accepted `main` commit. Use the repository skills for branch/PR/release procedures; do not put signing credentials in the repository.

Repository administrators can reconcile branch rules with `./scripts/configure-github-rulesets.sh`. It configures an active `main` ruleset with pull requests, resolved conversations, linear history, clang-format, gersemi, build, and CodeQL checks, and blocked force-pushes/deletions; it intentionally requires zero approvals for the sole maintainer.
