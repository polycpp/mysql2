# Port Checklist

Use this checklist as a phase tracker. Update each item explicitly.

Legend:

- `[ ]` not started
- `[-]` in progress
- `[x]` done

## Intake

- [ ] target repo scaffolded
- [ ] GitHub repo created under `polycpp`
- [ ] GitHub repo visibility verified private
- GitHub repo URL:
- GitHub repo private: no
- [ ] baseline readiness passed
- [ ] upstream repo cloned or updated locally
- [ ] upstream revision recorded in `docs/research.md`

## Upstream Analysis

- [ ] repo layout inspected
- [ ] entry points identified
- [ ] important implementation files listed
- [ ] test directories identified
- [ ] implementation risks recorded

## Existing Companion Alignment

- [ ] existing companion repos inspected for conventions
- [ ] CMake target and alias pattern aligned or deviation recorded
- [ ] public header layout planned
- [ ] detail/private header strategy planned
- [ ] aggregator header strategy decided
- [ ] example strategy recorded
- [ ] README structure aligned with companion repos
- [ ] deliberate deviations recorded in `docs/divergences.md`

## Polycpp Ecosystem Reuse

- [ ] polycpp core modules searched for reusable primitives
- [ ] polycpp core reuse decisions recorded
- [ ] companion libraries searched for reusable APIs
- [ ] companion reuse decisions recorded
- [ ] new local abstractions justified

## Dependency/API Analysis

- [ ] dependency/API analysis tool run
- [ ] published npm artifact inspected or not-applicable decision recorded
- [ ] direct dependencies classified
- [ ] dependency ownership decisions recorded
- [ ] dependency licenses classified
- [ ] dependency license strategy recorded
- [ ] GPL/AGPL/LGPL/MPL impacts reviewed
- [ ] Node.js API usage reviewed
- [ ] JavaScript API usage reviewed

## Scope

- [ ] `v0` scope written in `docs/research.md`
- [ ] deferred features listed in `docs/divergences.md`
- [ ] `polycpp` module dependencies listed
- [ ] missing `polycpp` primitives listed
- [ ] external SDK/native driver strategy recorded
- [ ] compatibility foundation review completed
- [ ] security and fail-closed review completed

## API Mapping

- [ ] major public `v0` APIs mapped
- [ ] direct vs adapted vs deferred status recorded
- [ ] dynamic typing adaptations recorded
- [ ] framework object boundary reviewed

## Testing

- [ ] unit test areas listed
- [ ] integration test areas listed
- [ ] upstream tests or fixtures to adapt listed
- [ ] security and fail-closed tests listed
- [ ] release-blocking behaviors listed

## Documentation Site

- [ ] docs scaffold installed
- [ ] docs build command recorded
- [ ] GitHub Pages workflow present
- [ ] `docs/build/` ignored
- [ ] generated placeholder docs tracked for replacement before public release

## Pre-Coding Gate

- [ ] `docs/research.md` is no longer placeholder-only
- [ ] `docs/dependency-analysis.md` is no longer placeholder-only
- [ ] `docs/api-mapping.md` is no longer placeholder-only
- [ ] `docs/divergences.md` is no longer placeholder-only
- [ ] `docs/test-plan.md` is no longer placeholder-only
- [ ] strict readiness passed

## Implementation

- [ ] implementation started after strict readiness
- [ ] tests added as features are implemented
- [ ] new divergences documented immediately

## Validation

- [ ] targeted tests pass
- [ ] README examples match actual code
- [ ] remaining gaps are documented

## Public Release

- [ ] production-grade quality confirmed
- [ ] public documentation ready
- [ ] generated docs placeholder pages replaced with real public documentation
- [ ] `python3 docs/build.py` passes
- [ ] third-party license notices complete
- [ ] GitHub repo visibility changed to public
