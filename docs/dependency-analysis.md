# Dependency and JavaScript API Analysis

Run `scripts/analyze-upstream-js.py` after upstream intake, then consume `.tmp/dependency-analysis.json` to fill this file.

- package: TODO
- package version: TODO
- package root: TODO
- analyzer json: `.tmp/dependency-analysis.json`
- published npm artifact path: TODO
- published npm artifact analyzed: TODO
- include dev dependencies: no
- dependency source install used: TODO
- companion root checked: `/data/work/lib`

## Package entry metadata

- main: TODO
- module: TODO
- types: TODO
- exports: TODO
- bin: TODO
- missing declared entries in repo clone: TODO
- TypeScript source files detected: TODO
- source-vs-published artifact decision: TODO

## Direct dependencies

- TODO

## Dependency ownership decisions

For every direct runtime dependency, choose exactly one outcome:

- use existing polycpp companion
- create separate private polycpp companion repo
- implement private helper in this repo
- optional adapter
- deferred or unsupported feature

Before choosing `implement private helper in this repo`, verify that neither
base `polycpp` nor an existing companion library already provides the required
behavior. Record that decision in `docs/research.md` under
`## Polycpp ecosystem reuse analysis`.

For every dependency, also choose a license strategy:

- permissive dependency ok with notice
- use existing companion license
- GPL-compatible repo or defer
- AGPL-compatible repo or defer
- optional GPL adapter or defer
- optional AGPL adapter or defer
- document LGPL obligations and linking model
- keep MPL code separated or replace/defer
- clean-room replacement
- dev/test-only, not shipped
- manual license review required (analyzer-only state; strict readiness fails until this is resolved)

License evidence must be concrete enough for another agent to audit. Acceptable examples:

- package.json license field
- manual SPDX check: LICENSE confirms MIT
- existing companion repo license reviewed
- dev/test-only dependency, not shipped

Do not leave analyzer-only evidence such as `unknown`, `unverified`, or `heuristic` in this table before strict readiness.

| Package | Kind | Requested | Installed | License | License evidence | License impact | License strategy | Affects repo license | Deps | Source files | Node API calls | JS API calls | Recommendation | Rationale |
|---|---|---|---|---|---|---|---|---|---:|---:|---:|---:|---|---|
| TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO | 0 | 0 | 0 | 0 | TODO | TODO |

## License impact summary

- upstream package license: TODO
- repo license decision: TODO
- GPL/AGPL dependencies: TODO
- LGPL/MPL dependencies: TODO
- permissive dependencies requiring notices: TODO
- dev/test-only dependencies excluded from shipped artifacts: TODO
- dependency license notices to add to `THIRD_PARTY_LICENSES.md`: TODO

## Transitive dependency summary

- TODO

## Runtime API usage

### Target package

- entry points analyzed: TODO
- source files analyzed by analyzer: TODO
- source files manually inspected: TODO
- external imports seen from target: TODO

### Analyzer porting gates

- polycpp reuse hints consumed: TODO
- security hints consumed: TODO
- security-sensitive package: TODO

### Node.js API usage

- TODO

### JavaScript API usage

- TODO

### Framework object boundary usage

- analyzer-reported target-package framework object accesses: TODO
- analyzer-reported dependency framework object accesses: TODO
- manual review decision: TODO

## Porting decisions

- TODO

Each porting decision should be consistent with the ecosystem reuse decisions
recorded in `docs/research.md`.

## Analyzer warnings

- TODO

If the analyzer emitted no warnings, replace this section with:

- none emitted by analyzer

If the analyzer emitted warnings, record each warning and the agent response. For TypeScript fallback warnings, state which source files and tests were manually inspected before strict readiness.
