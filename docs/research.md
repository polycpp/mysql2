# Research

Run `scripts/intake-upstream.py` before filling this file manually.

- package:
- npm url:
- source url:
- upstream version basis:
- upstream revision analyzed:
- upstream default branch:
- license:
- license evidence:
- category:

## Package purpose

- TODO

## Runtime assumptions

- browser: TODO
- node.js: TODO
- filesystem: TODO
- network: TODO
- crypto: TODO
- terminal: TODO

## Dependency summary

- package.json present: TODO
- hard dependencies: TODO
- peer dependencies: TODO
- optional dependencies: TODO
- package exports: TODO
- package types: TODO
- package bin: TODO
- dependency analysis report: `docs/dependency-analysis.md`

## Upstream repo layout summary

- Clone path used for analysis: TODO
- TODO

## Entry points used by consumers

- TODO

## Important files and why they matter

- TODO

## Files likely irrelevant to the C++ port

- TODO

## Test directories worth mining first

- TODO

## Implementation risks discovered from the source layout

- TODO

## Companion repo alignment

- companion repos inspected: TODO
- CMake target and alias pattern: TODO
- public header layout: TODO
- detail/private header strategy: TODO
- aggregator header strategy: TODO
- examples strategy: TODO
- documentation site strategy: TODO
- deliberate deviations from existing companions: TODO

## Polycpp ecosystem reuse analysis

- polycpp core paths inspected: TODO
- polycpp core types/functions selected: TODO
- polycpp core types/functions rejected: TODO
- companion libs inspected for reusable APIs: TODO
- companion libs selected for reuse: TODO
- companion libs rejected or deferred: TODO
- new local abstractions introduced: TODO
- reuse risks or integration gaps: TODO

## External SDK and native driver strategy

- upstream external services/protocols: TODO
- native SDKs/client libraries to use: TODO
- SDKs/protocols explicitly not reimplemented: TODO
- adapter/linking strategy: TODO
- test environment needs: TODO

## Compatibility foundation review

- downstream dependency role: TODO
- native substitution risk: TODO
- upstream implementation data to preserve: TODO
- generated or vendored data plan: TODO
- compatibility fixture strategy: TODO

## Security and fail-closed review

- security-sensitive behavior: TODO
- trust boundary: TODO
- supported protocol or algorithm matrix: TODO
- unsupported behavior and fail-closed policy: TODO
- key, secret, credential, or user-controlled input handling: TODO
- misuse cases that must be tested: TODO

## Core use cases

- TODO

## Key features to port first

- TODO

## Features to defer

- TODO

## v0 scope

- port version: 0.1.0
- versioning note: port version is independent from upstream versioning
- supported APIs: TODO
- unsupported APIs: TODO
- dependency plan: TODO
- polycpp modules to use: TODO
- missing polycpp primitives: TODO
