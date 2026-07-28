---
name: release-macos-plugin
description: Prepare and publish a signed, notarized macOS Input Activity plugin release. Use for release-candidate dispatches from main, stable SemVer tags on main, version/changelog work, signing/notarization setup checks, release assets, and release-note validation.
---

# Release macOS Plugin

1. Create a release-preparation pull request to `main`. Set `buildspec.json` to the intended SemVer version and add curated user-facing notes to `CHANGELOG.md`; validate the CI-equivalent macOS build.
2. Confirm repository secrets exist: `MACOS_CERTIFICATE_P12`, `MACOS_CERTIFICATE_PASSWORD`, `KEYCHAIN_PASSWORD`, `MACOS_SIGNING_IDENTITY`, `APPLE_ID`, `APPLE_APP_SPECIFIC_PASSWORD`, and `APPLE_TEAM_ID`. Never place these values in a file, log, commit, or issue.
3. Dispatch the release-candidate workflow on the accepted `main` commit with `X.Y.Z` and `N`. It creates immutable `vX.Y.Z-rc.N` prerelease assets. Verify Developer ID signatures, notarization/stapling, checksum, bundle layout, and GitHub release metadata.
4. After RC acceptance, create and push exact `vX.Y.Z` tag on that `main` commit. The stable workflow rejects a tag that differs from `buildspec.json`.
5. Verify release assets: signed `.pkg`, zipped `.plugin`, and `SHA256SUMS.txt`. Verify `pkgutil --check-signature`, `spctl --assess --type install --verbose`, `xcrun stapler validate`, and checksum output. Confirm release notes contain only the matching changelog section.
