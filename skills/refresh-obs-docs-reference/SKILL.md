---
name: refresh-obs-docs-reference
description: Audit the complete official OBS documentation site and refresh this repository's concise, version-pinned navigation reference. Use when buildspec.json changes its OBS Studio dependency or when a maintainer requests an OBS documentation audit.
---

# Refresh OBS Documentation Reference

1. Read `buildspec.json` and record its exact `dependencies.obs-studio.version`. This is the
   supported API version, not the version currently displayed by the live documentation site.
2. Review the whole navigation at <https://docs.obsproject.com>, including every Core Concepts
   page and all sections under OBS Core, Modules, Core API Objects, Platform/Utility, Callbacks,
   Graphics, Media I/O, and OBS Studio Frontend API. Do not limit the audit to the immediate task.
3. Update `docs/obs-reference.md` with the review date, displayed live-doc version, complete
   navigation inventory, working official links, and task map. Keep it a concise navigation guide;
   do not copy upstream documentation into this repository.
4. Reconcile the live documentation against the pinned OBS version. Flag version drift clearly and
   point agents to the matching OBS source or generated build artifacts for exact declarations,
   availability, ownership, callbacks, and thread/context rules.
5. Report pages or sections added, removed, renamed, or materially changed, plus compatibility
   risks for this plugin. Preserve the guide's source-ID and input-privacy safeguards.
6. Verify each listed URL resolves and that every top-level documentation area appears exactly once
   in the inventory. If this is an OBS dependency upgrade, validate relevant APIs before completing
   the upgrade pull request.
