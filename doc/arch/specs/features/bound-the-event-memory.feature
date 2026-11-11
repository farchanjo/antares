Feature: Bound The Event Memory
  As the operator
  I want one bounded entry per object, shared by every view
  So that a large cluster opens without the app eating a gigabyte

  # One scenario per FR of spec.md, named with the FR id so the link is
  # machine-checkable. Units are bytes; the measured baseline lives in the spec.

  Scenario: FR-001 one entry per object keyed by scope and uid
    Given the feature specification mandates "FR-001 one entry per object keyed by scope and uid"
    When the implementation satisfies: two views resolve the same scope and uid
    Then the observable contract holds: both receive the same stored entry

  Scenario: FR-002 the stored shape is a projection
    Given the feature specification mandates "FR-002 the stored shape is a projection"
    When the implementation satisfies: an object carrying managedFields, annotations and Secret data is stored
    Then the observable contract holds: none of the three reaches the entry

  Scenario: FR-003 the per-object cost is bounded
    Given the feature specification mandates "FR-003 the per-object cost is bounded"
    When the implementation satisfies: 5000 fixture pods are stored through a counting allocator
    Then the observable contract holds: the store allocates at most 2560000 bytes and no entry exceeds 1024

  Scenario: FR-004 retention is bounded by admission
    Given the feature specification mandates "FR-004 retention is bounded by admission"
    When the implementation satisfies: a scope at its ceiling receives another object
    Then the observable contract holds: the object is refused, counted, and the scope is truncated

  Scenario: FR-005 a watch frame is parsed once
    Given the feature specification mandates "FR-005 a watch frame is parsed once"
    When the implementation satisfies: 5000 frames arrive over C chunks
    Then the observable contract holds: framesParsed equals 5000 and bufferCompactions is at most C

  Scenario: FR-006 a delta reaches the store by key
    Given the feature specification mandates "FR-006 a delta reaches the store by key"
    When the implementation satisfies: a MODIFIED arrives for a stored uid and a DELETED for an unknown uid
    Then the observable contract holds: the first updates in place and the second is counted, not an error

  Scenario: FR-007 a relist reconciles the scope
    Given the feature specification mandates "FR-007 a relist reconciles the scope"
    When the implementation satisfies: an expiry is followed by a paginated relist
    Then the observable contract holds: unmarked rows are swept once after the final page and the count equals the relist

  Scenario: FR-008 the store announces each batch in row terms
    Given the feature specification mandates "FR-008 the store announces each batch in row terms"
    When the implementation satisfies: a ChangeSet is replayed onto a row mirror
    Then the observable contract holds: the mirror reproduces the store's rows exactly

  Scenario: FR-009 the store lives on one thread
    Given the feature specification mandates "FR-009 the store lives on one thread"
    When the implementation satisfies: a batch is decoded off the model thread
    Then the observable contract holds: it crosses as a value and is applied on the model thread
