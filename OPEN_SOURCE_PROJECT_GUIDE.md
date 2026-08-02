# Open-Source Project Structure and Collaboration Guide

This document describes a practical, high-level structure for an open-source
project. It is intended to make the project easy to understand, safe to change,
and welcoming to contributors while preserving a reliable path to releases.

## 1. Project foundation

An open-source repository should make its purpose, boundaries, and expectations
easy to find from the root directory.

| Area | Purpose | Typical files or locations |
| --- | --- | --- |
| Product overview | Explain what the project does, who it serves, and how to install or use it. | `README.md` |
| License and attribution | State reuse permissions and acknowledge required upstream work. | `LICENSE`, notices |
| Contribution guide | Explain how to propose, build, test, and submit changes. | `CONTRIBUTING.md` |
| Security process | Provide a private path to report vulnerabilities and set response expectations. | `SECURITY.md` |
| Change history | Record user-facing changes by release. | `CHANGELOG.md` |
| Governance | Define maintainers, decision-making, and escalation paths as the community grows. | `GOVERNANCE.md` (when needed) |
| Community standards | Set expectations for respectful participation. | `CODE_OF_CONDUCT.md` (recommended) |
| Automation | Keep repeatable checks, builds, and releases under version control. | `.github/`, `scripts/`, CI configuration |
| Source and tests | Separate product code, tests, documentation, and bundled dependencies clearly. | `src/`, `tests/`, `docs/`, `deps/` |

Every project benefits from a short issue template and pull-request template.
They turn informal reports into actionable context: the problem, expected
behavior, scope, validation, risks, and user-visible release notes.

## 2. Roles and decision-making

Healthy projects distinguish between participation and authority without making
contribution unnecessarily difficult.

- Users report problems, share feedback, and test releases.
- Contributors propose documentation, code, tests, and design improvements.
- Reviewers help ensure changes are correct, maintainable, secure, and in scope.
- Maintainers steward the roadmap, repository settings, releases, and final
  merge decisions.
- Release managers (often maintainers) coordinate versioning, release notes,
  signing, publication, and rollback decisions.

Document who holds each responsibility and how decisions are made. For small
projects, a maintainer may fill several roles. As the project grows, use
`CODEOWNERS`, a governance document, or an explicit maintainer list to clarify
ownership and review expectations.

## 3. Branch management

Use protected long-lived branches and short-lived topic branches. Protecting
the integration branches prevents accidental history rewrites and ensures all
changes pass the same review and automation gates.

```text
topic branch ── pull request ──> main ── tag ──> release
feature/*, fix/*, chore/*        protected stable history
hotfix/* (from affected tag) ───> main and a patch release
```

### Recommended branch policy

- `main` is the protected, always-releasable integration branch. All ordinary
  changes arrive there through a pull request.
- `feature/<short-kebab-name>` is for new behavior.
- `fix/<short-kebab-name>` is for defect corrections.
- `chore/<short-kebab-name>` is for maintenance, tooling, or non-product work.
- `hotfix/<short-kebab-name>` is reserved for an urgent correction to a stable
  release; create it from the affected release tag or supported stable commit.
- Create ordinary topic branches from a current `main`, keep them focused, and delete
  them after merge.
- Do not push directly to protected branches, force-push them, or bypass
  required checks.
- Require pull requests, resolved review conversations, linear history, and
  required CI checks on protected branches. Choose an approval count that fits
  the active maintainer group; a sole-maintainer project may rely on automated
  checks and self-review discipline.

Use clear, conventional commit subjects such as `feat:`, `fix:`, `docs:`,
`test:`, `refactor:`, `build:`, `ci:`, and `chore:`. Each commit should be
small enough to review and revert independently. Rebase or otherwise reconcile
with current `main` before merging when required by the project’s history
policy.

## 4. Workflows for different kinds of updates

The same change lifecycle applies to all work: discuss where useful, branch,
implement, validate, open a pull request, review, merge, and communicate the
result. The checks and coordination scale with risk.

| Update type | Expected workflow |
| --- | --- |
| Documentation | Create a focused topic branch, verify links and instructions, then submit a pull request. Use an issue first when changing policy or user-facing guidance significantly. |
| Bug fix | Reproduce or describe the problem, add a regression test when practical, assess compatibility, validate the affected behavior, and document user-visible fixes in the changelog. |
| Feature | Start with an issue or design discussion for non-trivial scope, define acceptance criteria, implement incrementally, include tests and documentation, and provide screenshots or recordings for visual changes. |
| Refactor | State the preserved behavior, keep mechanical cleanup separate from behavior changes where possible, and run targeted and full validation. |
| Dependency update | Identify upstream version and license/security impact, note behavior or build-system changes, test supported platforms, and isolate bundled dependency updates from unrelated work. |
| Security fix | Follow `SECURITY.md`; keep vulnerability details private until a coordinated fix is ready, restrict access to sensitive information, validate the remedy, and publish an advisory or release note at disclosure. |
| Release | Update the version and changelog in a pull request to `main`, validate the exact merged revision or a release candidate, create a signed version tag from `main`, publish artifacts, and announce the release. |
| Urgent stable fix | Branch `hotfix/<name>` from the affected release tag only when a normal `main` pull request cannot wait. Review and test the narrow correction, merge it into `main`, create a patch-release tag, and publish the patch. Any additional compatible changes are made separately on current `main`. |

For every workflow, the pull request should be the lasting change record. It
should explain why the change exists, its user or compatibility impact, tests
performed, known risks, follow-up work, and any release-note entry.

## 5. Pull-request and CI workflow

Pull requests are the default integration mechanism. A good pull request is
small, scoped to one concern, and ready for a reviewer who does not know the
author’s local environment.

1. Synchronize the topic branch with the current `main` branch.
2. Run formatters, linting, unit or integration tests, and the relevant build
   locally where feasible.
3. Open a pull request to `main`, using the repository template.
4. CI independently runs formatting, build, test, and security checks on the
   pull request and `main`.
5. Reviewers request changes or approve; authors resolve conversations and
   update validation evidence.
6. Merge using the repository’s approved history strategy after all required
   checks pass.

Automated checks should cover formatting, compilation, tests, dependency or
license policy where applicable, static analysis, and release packaging. Keep
secrets in the CI platform’s secret store, never in source control or pull
request logs.

## 6. Contributor experience

The first contribution should be understandable without private knowledge.
Maintain these contributor-facing paths:

- A quick-start section with prerequisites, build, test, and local run steps.
- Clearly labeled `good first issue` and `help wanted` issues with enough
  context to begin safely.
- Issue templates for bugs and feature requests, plus a discussion space for
  questions and early design feedback.
- A contribution guide that states branch naming, commit conventions, test
  expectations, reviewer response targets where possible, and how to update
  documentation and release notes.
- A code of conduct and a private security-reporting channel.
- Attribution in release notes and project acknowledgements, according to
  contributor preferences and the project’s policies.

Welcome contributions of more than code: bug reports, documentation,
translations, design feedback, testing across platforms, triage, and community
support all improve the project.

## 7. Release and maintenance practices

Use semantic versioning when it matches the project: breaking changes increase
the major version, backward-compatible features increase the minor version,
and compatible fixes increase the patch version. Keep the changelog
user-focused and write it as changes land rather than reconstructing it at
release time.

Before a stable release, build the exact revision that will be tagged, validate
the produced artifact on supported environments, and publish a release
candidate for substantial or high-risk changes. Stable release automation should
verify that the tag matches the declared version and is based on `main` before
signing or publishing assets. Provide checksums and provenance or signatures
where the distribution channel supports them.

Maintain a rollback plan: retain prior artifacts, identify the last known-good
release, document any required downgrade or migration steps, and treat a
critical regression as a release-blocking issue.

## 8. Applying this model in this repository

For a single maintainer, this repository can use the simpler protected-`main`
model described here: contributions use the `feature/`, `fix/`, `chore/`, and
when necessary `hotfix/` naming convention, and every change arrives through a
pull request. Releases are version tags created from `main`; a hotfix produces
a patch release after the fix is merged. Formatting and static-security
automation remain required gates. The repository’s `CONTRIBUTING.md`,
`SECURITY.md`, pull-request template, and GitHub workflows should be updated
together before this policy becomes operational.

For changes affecting global input capture, contributors must preserve the
project’s privacy expectations: do not log keystroke contents, preserve the
actionable macOS Accessibility guidance, and manually validate affected OBS
source behavior.
