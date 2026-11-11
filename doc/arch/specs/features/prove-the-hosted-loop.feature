Feature: Prove The Hosted Loop
  As the developer
  I want the hosted loop measured on the acceptance platform
  So that a Darwin-only design is not taken on faith

  # A spike, run by hand on the macOS host. Each scenario is a way the design
  # can be wrong and the measurement that settles it.

  Scenario: FR-001 the notifier fires on a kqueue fd
    Given the feature specification mandates "FR-001 the notifier fires on a kqueue fd"
    When the implementation satisfies: the peer writes to the registered socket
    Then the observable contract holds: the Qt loop wakes with no timer armed

  Scenario: FR-002 the turn does not block the paint
    Given the feature specification mandates "FR-002 the turn does not block the paint"
    When the implementation satisfies: the transport saturates for 30 seconds
    Then the observable contract holds: painted frames stay at or above 80 percent of idle

  Scenario: FR-003 a modal does not deadlock
    Given the feature specification mandates "FR-003 a modal does not deadlock"
    When the implementation satisfies: a completion opens and dismisses a modal
    Then the observable contract holds: no deadlock and the transport keeps running

  Scenario: FR-004 nested loops still deliver socket events
    Given the feature specification mandates "FR-004 nested loops still deliver socket events"
    When the implementation satisfies: a live resize and a held menu run for 10 seconds
    Then the observable contract holds: events arrive and none is lost

  Scenario: FR-005 nextDeadline drives a QTimer
    Given the feature specification mandates "FR-005 nextDeadline drives a QTimer"
    When the implementation satisfies: a timer is submitted through the engine
    Then the observable contract holds: it fires on the main thread within its tolerance

  Scenario: FR-006 the turn honours a deadline
    Given the feature specification mandates "FR-006 the turn honours a deadline"
    When the implementation satisfies: a completion sleeps 50 milliseconds
    Then the observable contract holds: the next turn does not start past the budget

  Scenario: FR-007 App Nap does not stall the loop
    Given the feature specification mandates "FR-007 App Nap does not stall the loop"
    When the implementation satisfies: the window is occluded for 60 seconds
    Then the observable contract holds: a timer still fires within an order of magnitude of its period

  Scenario: FR-008 one loop carries thirty-two watches
    Given the feature specification mandates "FR-008 one loop carries thirty-two watches"
    When the implementation satisfies: thirty-two connections deliver frames while the window repaints
    Then the observable contract holds: the loop keeps up and the frame rate stays at or above 80 percent of idle
