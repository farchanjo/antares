# Tasks: Scan The Projection

## Task Breakdown

- [ ] T001 Add the fixture set: the pod, a CRD with `additionalPrinterColumns`, a
      Secret with `data`, and one case per escape (FR-004).
- [ ] T002 Add the allocation counter to the test support and the failing case
      that pins the measured count (FR-002).
- [ ] T003 Add `ScalarScan` with the skip walk and a bounded depth (FR-002,
      FR-003).
- [ ] T004 Add offset-reporting failures for a truncated and a mis-nested
      document (FR-003).
- [ ] T005 Add string unescaping including `\uXXXX` and surrogate pairs (FR-004).
- [ ] T006 Move `decodeEntry` onto the scanner; keep `Value::parse` as the
      fallback for a shape it refuses (FR-001).
- [ ] T007 Add the field-by-field comparison case between both paths (FR-001).
- [ ] T008 Add the key lookup that stops at `metadata.name` (FR-005).
- [ ] T009 Add the 4× case over the same fixture (FR-006).
- [ ] T010 Add `scanFailures` to `KubeStats` (observability).

## Dependencies

T003 blocks T004, T005 and T006. T001 and T002 land first and stay red until T006.
Feature 005 must land before this one: the scanner produces its `#Entry`, and its
fallback calls the projection.
