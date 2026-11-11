Feature: Render The Resource List
  As the operator
  I want to scroll a large list while it fills
  So that the window never freezes on open

  # One scenario per FR of spec.md. The model holds no row data: the store is
  # the only copy.

  Scenario: FR-001 the model holds no data
    Given the feature specification mandates "FR-001 the model holds no data"
    When the implementation satisfies: a scope with 5000 objects is built
    Then the observable contract holds: the model footprint stays under 4 KiB

  Scenario: FR-002 pages arrive through fetchMore
    Given the feature specification mandates "FR-002 pages arrive through fetchMore"
    When the implementation satisfies: the view reaches the last row with a continue token
    Then the observable contract holds: the next page is requested and inserted as one range

  Scenario: FR-003 roles are filled in one call
    Given the feature specification mandates "FR-003 roles are filled in one call"
    When the implementation satisfies: a row is asked for its roles
    Then the observable contract holds: multiData fills the span in one call

  Scenario: FR-004 a ChangeSet becomes row operations
    Given the feature specification mandates "FR-004 a ChangeSet becomes row operations"
    When the implementation satisfies: a batch with removed, inserted and changed rows is applied
    Then the observable contract holds: each becomes its matching model operation

  Scenario: FR-005 a reorder is a move
    Given the feature specification mandates "FR-005 a reorder is a move"
    When the implementation satisfies: the ordering key changes
    Then the observable contract holds: beginMoveRows is used and selection survives

  Scenario: FR-006 layout is batched and uniform
    Given the feature specification mandates "FR-006 layout is batched and uniform"
    When the implementation satisfies: the view is configured
    Then the observable contract holds: layout is batched and item sizes are uniform

  Scenario: FR-007 the header never measures contents
    Given the feature specification mandates "FR-007 the header never measures contents"
    When the implementation satisfies: a scope above 1000 rows is shown
    Then the observable contract holds: column widths stay fixed

  Scenario: FR-008 the model lives on the GUI thread
    Given the feature specification mandates "FR-008 the model lives on the GUI thread"
    When the implementation satisfies: a ChangeSet is produced off the GUI thread
    Then the observable contract holds: it crosses as a value through a queued connection
