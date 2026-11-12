# Tasks: Relax The API Path Name

## Task Breakdown

- [x] T001 Write the failing table case over the eleven name classes: upper, `_`,
      `:`, `.`, 253 bytes, 254 bytes, empty, `.`, `..`, `a/b`, `a%b` (FR-001,
      FR-004).
- [x] T002 Add `SEGMENT_CEILING` and `isPathSegment`; point `named` at it
      (FR-001, FR-004).
- [x] T003 Keep `isDnsLabel` on `inNamespace` and say in the header which
      function guards which (FR-002).
- [x] T004 Replace the single refusal message with the closed reason class and
      its mapping (FR-003).
- [ ] T005 Add the agreement case between the store rule and the path rule
      (FR-005).

## Dependencies

T001 first and red until T002. T003 and T004 are independent of T002 and can land
before it. T005 needs both rules in place.
