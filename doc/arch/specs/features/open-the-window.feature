Feature: Open The Window
  As the operator
  I want the app to open on my current context and close cleanly
  So that nothing is left half-written

  # One scenario per FR of spec.md. This feature only sequences components; it
  # owns no behaviour of its own.

  Scenario: FR-001 construction follows dependency order
    Given the feature specification mandates "FR-001 construction follows dependency order"
    When the implementation satisfies: the process starts
    Then the observable contract holds: objects are created in the stated order and every QObject takes its parent

  Scenario: FR-002 a failure unwinds what was built
    Given the feature specification mandates "FR-002 a failure unwinds what was built"
    When the implementation satisfies: the kubeconfig does not load
    Then the observable contract holds: the steps already done are undone and the exit is non-zero with a reason

  Scenario: FR-003 the loop is hosted, not run
    Given the feature specification mandates "FR-003 the loop is hosted, not run"
    When the implementation satisfies: the engine is attached
    Then the observable contract holds: a QSocketNotifier over waitHandle drives runOnce

  Scenario: FR-004 destruction is the reverse, after the drain
    Given the feature specification mandates "FR-004 destruction is the reverse, after the drain"
    When the implementation satisfies: exec returns
    Then the observable contract holds: the drain runs first and the engine is freed last

  Scenario: FR-005 signals reach the same path as a close
    Given the feature specification mandates "FR-005 signals reach the same path as a close"
    When the implementation satisfies: SIGTERM arrives
    Then the observable contract holds: a byte reaches the self-pipe and quit is called

  Scenario: FR-006 the first context comes from the kubeconfig
    Given the feature specification mandates "FR-006 the first context comes from the kubeconfig"
    When the implementation satisfies: the kubeconfig has no current context
    Then the observable contract holds: the window opens empty naming the file

  Scenario: FR-007 a second instance does not fight
    Given the feature specification mandates "FR-007 a second instance does not fight"
    When the implementation satisfies: two instances start at once
    Then the observable contract holds: neither corrupts the kubeconfig
