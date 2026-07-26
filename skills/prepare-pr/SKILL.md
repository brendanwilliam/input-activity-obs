---
name: prepare-pr
description: Prepare a governed pull request for this repository. Use when validating a feature, fix, chore, or release change; choosing develop or main as its target; writing commit and PR records; or performing the handoff checklist.
---

# Prepare Pull Request

1. Target `develop` for ordinary changes. Target `main` only for the release PR from `develop`.
2. Confirm a Conventional Commit subject and explanatory body for non-trivial work.
3. Run the applicable formatter and `cmake --preset macos-ci` plus `cmake --build --preset macos-ci`. Report any intentionally unrun validation and why.
4. Manually verify affected source types in OBS; for capture changes, verify both granted and missing Accessibility permission behavior.
5. Update `CHANGELOG.md` for user-visible behavior. Do not add a release note for internal-only work without explaining the omission in the PR.
6. Complete `.github/pull_request_template.md`: summary, behavior impact, validation, risks/follow-up, screenshots or recording for visual work, and release-note entry.
7. Rebase or otherwise preserve linear history before requesting merge. Required checks and resolved review conversations must be complete; approval is not required for the sole maintainer.
