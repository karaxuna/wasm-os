# Changesets

Release management for the publishable packages (`mklfs`, `wasm-os-sdk`,
`wasm-os`). `wasm-os-tests` is private and ignored; the firmware version in
the root `package.json` is a separate axis, managed by hand.

Flow:

1. With a change worth releasing, run `npx changeset` and pick the packages,
   bump type, and changelog line. Commit the generated file with the change.
2. At release time, `npm run version-packages` consumes pending changesets:
   bumps versions, cascades through internal dependency ranges, writes
   per-package CHANGELOGs. Commit the result.
3. `npm run release` runs all workspace tests, then publishes what changed
   in dependency order and git-tags each package (`mklfs@x.y.z`).

Docs: https://github.com/changesets/changesets
