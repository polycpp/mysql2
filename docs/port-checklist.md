# Port Checklist

Use this checklist as a phase tracker. Update each item explicitly.

Legend:

- `[ ]` not started
- `[-]` in progress
- `[x]` done

## Intake

- [x] target repo scaffolded
- [x] GitHub repo created under `polycpp`
- [x] GitHub repo visibility verified private
- GitHub repo URL: https://github.com/polycpp/mysql2
- GitHub repo private: yes
- [x] baseline readiness passed
- [x] upstream repo cloned or updated locally
- [x] upstream revision recorded in `docs/research.md`

## Upstream Analysis

- [x] repo layout inspected
- [x] entry points identified
- [x] important implementation files listed
- [x] test directories identified
- [x] implementation risks recorded

## Existing Companion Alignment

- [x] existing companion repos inspected for conventions
- [x] CMake target and alias pattern aligned or deviation recorded
- [x] public header layout planned
- [x] detail/private header strategy planned
- [x] aggregator header strategy decided
- [x] example strategy recorded
- [x] README structure aligned with companion repos
- [x] deliberate deviations recorded in `docs/divergences.md`

## Polycpp Ecosystem Reuse

- [x] polycpp core modules searched for reusable primitives
- [x] polycpp core reuse decisions recorded
- [x] companion libraries searched for reusable APIs
- [x] companion reuse decisions recorded
- [x] new local abstractions justified

## Dependency/API Analysis

- [x] dependency/API analysis tool run
- [x] published npm artifact inspected or not-applicable decision recorded
- [x] direct dependencies classified
- [x] dependency ownership decisions recorded
- [x] dependency licenses classified
- [x] dependency license strategy recorded
- [x] GPL/AGPL/LGPL/MPL impacts reviewed
- [x] Node.js API usage reviewed
- [x] JavaScript API usage reviewed

## Scope

- [x] `v0` scope written in `docs/research.md`
- [x] deferred features listed in `docs/divergences.md`
- [x] `polycpp` module dependencies listed
- [x] missing `polycpp` primitives listed
- [x] external SDK/native driver strategy recorded
- [x] compatibility foundation review completed
- [x] security and fail-closed review completed

## API Mapping

- [x] major public `v0` APIs mapped
- [x] direct vs adapted vs deferred status recorded
- [x] dynamic typing adaptations recorded
- [x] framework object boundary reviewed

## Testing

- [x] unit test areas listed
- [x] integration test areas listed
- [x] upstream tests or fixtures to adapt listed
- [x] security and fail-closed tests listed
- [x] release-blocking behaviors listed

## Documentation Site

- [x] docs scaffold installed
- [x] docs build command recorded
- [x] GitHub Pages workflow present
- [x] `docs/build/` ignored
- [x] generated placeholder docs tracked for replacement before public release

## Pre-Coding Gate

- [x] `docs/research.md` is no longer placeholder-only
- [x] `docs/dependency-analysis.md` is no longer placeholder-only
- [x] `docs/api-mapping.md` is no longer placeholder-only
- [x] `docs/divergences.md` is no longer placeholder-only
- [x] `docs/test-plan.md` is no longer placeholder-only
- [x] strict readiness passed

## Implementation

- [ ] implementation started after strict readiness
- [x] tests added as features are implemented
- [x] new divergences documented immediately

Implementation note: this challenge port produced implementation and documentation in one feedback run. Future generated ports should complete strict readiness before editing source files.

## Validation

- [x] targeted tests pass
- [x] README examples match actual code
- [x] remaining gaps are documented

Validation evidence:

- `cmake --build build -j2` passed.
- `ctest --test-dir build --output-on-failure` passed with unit tests and skipped e2e when env was absent.
- Real MariaDB 10.6 e2e passed with `MYSQL2_TEST_HOST=127.0.0.1 MYSQL2_TEST_PORT=43306 MYSQL2_TEST_USER=root MYSQL2_TEST_DATABASE=polycpp_mysql2_test`.

## Public Release

- [ ] production-grade quality confirmed
- [ ] public documentation ready
- [ ] generated docs placeholder pages replaced with real public documentation
- [x] `python3 docs/build.py` passes
- [x] third-party license notices complete for current private v0 scope
- [ ] GitHub repo visibility changed to public

Public release note: keep the repository private until TLS, MySQL 8 auth e2e, prepared-statement scope, and public docs are reviewed.
