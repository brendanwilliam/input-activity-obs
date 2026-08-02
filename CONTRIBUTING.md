# Contributing

## Workflow

Create `feature/<kebab-title>`, `fix/<kebab-title>`, or `chore/<kebab-title>` from an up-to-date `develop`.
Open an ordinary-change pull request to `develop`, which integrates parallel work. When the integrated work is
ready to release, open a separate `develop` to `main` promotion pull request. Both branches are protected:
direct pushes, force-pushes, and deletion are not allowed.

The promotion PR is the only route to `main` and must satisfy all of its required checks. Working through
`develop` avoids repeatedly running the protected-`main` gate for every in-progress feature; it does not bypass
that gate for code that reaches `main`.

Before opening feature branches, ensure `develop` contains current `main`. When first adopting this workflow, or
after a main-only change, open a synchronization pull request from `main` to `develop` before accepting ordinary
work into `develop`.

Use Conventional Commit subjects (`feat:`, `fix:`, `docs:`, `chore:`, `refactor:`, `test:`, `build:`, or `ci:`). Explain non-trivial work in the commit body. Keep commits focused and rebase to preserve linear history.

## Before opening a pull request

Run formatting and the macOS CI-equivalent build. Manually exercise affected OBS sources and verify Accessibility permission behavior for capture changes. Complete every section of the pull request template, including a release-note entry in `CHANGELOG.md` when user-visible behavior changes. Repeat the applicable validation after integrating changes into `develop` and before promoting it to `main`.

## Releases

Stable versions are SemVer values in `buildspec.json`, promoted from `develop` through a pull request to `main`.
Validate release candidates from `main`, then create the matching stable tag on the accepted `main` commit. Use the repository skills for branch/PR/release procedures; do not put signing credentials in the repository.

Repository administrators can reconcile branch rules with `./scripts/configure-github-rulesets.sh`. It configures
an active `develop` ruleset with pull requests, resolved conversations, linear history, and blocked
force-pushes/deletions. Its `main` ruleset adds clang-format, gersemi, build, and CodeQL checks; it intentionally
requires zero approvals for the sole maintainer.
