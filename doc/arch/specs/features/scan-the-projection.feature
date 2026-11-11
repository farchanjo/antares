Feature: Scan The Projection
  As the operator
  I want a burst of events to cost a fraction of today
  So that a busy cluster does not slow the window

  # One scenario per FR of spec.md. The scanner is the third parser in the tree
  # and the most exposed; FR-003 and FR-004 are the contract.

  Scenario: FR-001 the scanner produces the same entry as the parser
    Given the feature specification mandates "FR-001 the scanner produces the same entry as the parser"
    When the implementation satisfies: both paths read the fixture set
    Then the observable contract holds: the entries are equal field by field

  Scenario: FR-002 skipped fields allocate nothing
    Given the feature specification mandates "FR-002 skipped fields allocate nothing"
    When the implementation satisfies: a document carrying managedFields and annotations is scanned
    Then the observable contract holds: the allocation count stays at the measured number

  Scenario: FR-003 a skipped value is still validated
    Given the feature specification mandates "FR-003 a skipped value is still validated"
    When the implementation satisfies: a truncated and a mis-nested document are scanned
    Then the observable contract holds: both fail with their offset

  Scenario: FR-004 escapes are resolved for kept fields
    Given the feature specification mandates "FR-004 escapes are resolved for kept fields"
    When the implementation satisfies: a name carrying an escaped character is scanned
    Then the observable contract holds: the stored value is the decoded one

  Scenario: FR-005 a key is found without building the document
    Given the feature specification mandates "FR-005 a key is found without building the document"
    When the implementation satisfies: a resource name is looked up
    Then the observable contract holds: the walk stops at the name

  Scenario: FR-006 the saving is measured
    Given the feature specification mandates "FR-006 the saving is measured"
    When the implementation satisfies: both paths run over the same fixture
    Then the observable contract holds: the scanner is at least four times faster
