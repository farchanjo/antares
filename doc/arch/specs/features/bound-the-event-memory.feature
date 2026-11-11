Feature: Bound The Event Memory
  As the operator
  I want one bounded entry per object instead of a full document per row
  So that a large cluster opens without the app eating a gigabyte

  # One scenario per FR of spec.md, named with the FR id so the link is
  # machine-checkable. The measured baseline lives in the spec body.

  Scenario: FR-001 one representation per uid
    Given the feature specification mandates "FR-001 one representation per uid"
    When the implementation satisfies: two views list the same scope and resolve an object by uid
    Then the observable contract holds: both receive the same stored entry

  Scenario: FR-002 the stored shape is a projection
    Given the feature specification mandates "FR-002 the stored shape is a projection"
    When the implementation satisfies: an object carrying managedFields is stored
    Then the observable contract holds: the stored entry carries no managedFields and no full body

  Scenario: FR-003 the per-object cost is bounded
    Given the feature specification mandates "FR-003 the per-object cost is bounded"
    When the implementation satisfies: 5000 pod-shaped objects are stored
    Then the observable contract holds: the store holds less than 2.5 MB

  Scenario: FR-004 retention is bounded by scope
    Given the feature specification mandates "FR-004 retention is bounded by scope"
    When the implementation satisfies: a scope exceeds its entry ceiling
    Then the observable contract holds: least-recently-updated entries are evicted and the count stays under the ceiling

  Scenario: FR-005 a watch frame is parsed once
    Given the feature specification mandates "FR-005 a watch frame is parsed once"
    When the implementation satisfies: 5000 frames are decoded
    Then the observable contract holds: the cost stays under one and a half parses per frame

  Scenario: FR-006 a delta reaches the store by uid
    Given the feature specification mandates "FR-006 a delta reaches the store by uid"
    When the implementation satisfies: a MODIFIED event arrives for a stored uid
    Then the observable contract holds: the entry is updated in place and the count is unchanged

  Scenario: FR-007 the store survives a 410 without duplicating
    Given the feature specification mandates "FR-007 the store survives a 410 without duplicating"
    When the implementation satisfies: an expiry is followed by a relist of the scope
    Then the observable contract holds: the entry count equals the relisted objects, not the sum
