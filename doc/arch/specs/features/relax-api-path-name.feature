Feature: Relax The API Path Name
  As the operator
  I want a name the server accepted to be fetchable
  So that the app never shows a row it cannot open

  # One scenario per FR of spec.md. The namespace stays strict on purpose: the
  # server requires DNS-1123 for it.

  Scenario: FR-001 the object name follows the server rule
    Given the feature specification mandates "FR-001 the object name follows the server rule"
    When the implementation satisfies: a 253-byte name with colon, uppercase and underscore is built
    Then the observable contract holds: it is accepted

  Scenario: FR-002 the namespace stays a DNS-1123 label
    Given the feature specification mandates "FR-002 the namespace stays a DNS-1123 label"
    When the implementation satisfies: a name carrying a colon is used as a namespace
    Then the observable contract holds: it is refused

  Scenario: FR-003 the refusal names the reason
    Given the feature specification mandates "FR-003 the refusal names the reason"
    When the implementation satisfies: an empty name is built
    Then the observable contract holds: the refusal names the empty rule

  Scenario: FR-004 the traversal property survives
    Given the feature specification mandates "FR-004 the traversal property survives"
    When the implementation satisfies: the names dot, dotdot, a slash and a percent are built
    Then the observable contract holds: all four are refused

  Scenario: FR-005 the store and the path agree
    Given the feature specification mandates "FR-005 the store and the path agree"
    When the implementation satisfies: one name per class is fed to both the store rule and the path
    Then the observable contract holds: they agree on every class
