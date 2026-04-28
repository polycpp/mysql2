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
- [x] current polycpp capability snapshot recorded
- [x] transport/listener capabilities revalidated against current polycpp
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
- [x] Node parity surface audit completed
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
- [x] Node parity surface review recorded

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
- [x] generated docs placeholders replaced with supported-scope user docs

## Pre-Coding Gate

- [x] `docs/research.md` is no longer placeholder-only
- [x] `docs/dependency-analysis.md` is no longer placeholder-only
- [x] `docs/api-mapping.md` is no longer placeholder-only
- [x] `docs/divergences.md` is no longer placeholder-only
- [x] `docs/test-plan.md` is no longer placeholder-only
- [x] strict readiness passed

## Implementation

- [x] implementation started after strict readiness
- [x] tests added as features are implemented
- [x] new divergences documented immediately

Implementation note: this challenge port produced implementation and documentation in one feedback run. Future generated ports should complete strict readiness before editing source files.

## Validation

- [x] targeted tests pass
- [x] README examples match actual code
- [x] remaining gaps are documented
- [x] `docs/test-plan.md` current validation records exact commands run

Validation evidence:

- `cmake --build build -j2` passed.
- `ctest --test-dir build --output-on-failure` passed with unit tests and skipped e2e when env was absent.
- Real MySQL 8.4.6 Docker e2e passed from an Ubuntu 22.04 helper container sharing the MySQL container network namespace.
- Real MariaDB 10.6 Docker e2e passed from an Ubuntu 22.04 helper container sharing the MariaDB container network namespace.
- Real MariaDB 10.6 verified TLS Docker e2e passed with `MYSQL2_TEST_SSL=1`, `MYSQL2_TEST_SSL_REJECT_UNAUTHORIZED=1`, `MYSQL2_TEST_SSL_VERIFY_IDENTITY=1`, and `MYSQL2_TEST_SSL_CA_FILE=/work/build/mariadb-tls/ca.pem`.
- `python3 docs/build.py` passed.

## Public Release

- [x] production-grade quality confirmed
- [x] public documentation ready for supported scope
- [x] generated docs placeholder pages replaced with real public documentation
- [x] `python3 docs/build.py` passes
- [x] third-party license notices complete
- [ ] GitHub repo visibility changed to public

Public release note: keep the repository private until the user confirms public visibility for this comprehensive driver port.
