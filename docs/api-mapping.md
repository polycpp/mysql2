# API Mapping

| Upstream symbol | C++ symbol | Status | Notes |
|---|---|---|---|
| TODO | TODO | TODO | TODO |

## Framework object boundary review

- Upstream reads or mutates framework/request/response/context objects: TODO
- Upstream fields or methods read: TODO
- Upstream fields or methods written: TODO
- C++ adapter boundary: TODO
- Partial mutation risk on validation failure: TODO

If the boundary overlaps with existing `polycpp` request, response, header,
stream, URL, or companion-library types, name the reused type here. Do not leave
the boundary as a custom local abstraction without a recorded justification in
`docs/research.md`.
