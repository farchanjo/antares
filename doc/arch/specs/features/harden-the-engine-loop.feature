Feature: Harden The Engine Loop
  As the operator
  I want every defect the hosted-loop review confirmed to be fixed
  So that a window inherits an engine that cannot touch freed memory

  # One scenario per FR of spec.md, named with the FR id so the link is
  # machine-checkable. The mode of operation does not change here; ADR-0020
  # records the hosted loop separately.

  Scenario: FR-001 cancellation removes the kernel registration
    Given the feature specification mandates "FR-001 cancellation removes the kernel registration"
    When the implementation satisfies: an op is cancelled and the descriptor becomes readable
    Then the observable contract holds: the completion runs once and no freed op is touched

  Scenario: FR-002 resolution settles once per job
    Given the feature specification mandates "FR-002 resolution settles once per job"
    When the implementation satisfies: two lookups of the same host resolve inside the watchdog window
    Then the observable contract holds: each connection settles exactly once

  Scenario: FR-003 the task queue is lock-free and multi-producer
    Given the feature specification mandates "FR-003 the task queue is lock-free and multi-producer"
    When the implementation satisfies: four producers post while the loop drains and fires a due timer
    Then the observable contract holds: every task runs once and no mutex is taken on the post or drain path

  Scenario: FR-004 the op store is read under its lock
    Given the feature specification mandates "FR-004 the op store is read under its lock"
    When the implementation satisfies: cancelPending walks the op store as the loop releases an op
    Then the observable contract holds: the walk never visits a freed op

  Scenario: FR-005 stop settles what is pending
    Given the feature specification mandates "FR-005 stop settles what is pending"
    When the implementation satisfies: stop returns with an op in flight
    Then the observable contract holds: the op completed with cancelled and never completes again

  Scenario: FR-006 stop is reversible
    Given the feature specification mandates "FR-006 stop is reversible"
    When the implementation satisfies: stop is followed by run on the same engine
    Then the observable contract holds: the loop serves work again

  Scenario: FR-007 the reported backend is the chosen one
    Given the feature specification mandates "FR-007 the reported backend is the chosen one"
    When the implementation satisfies: each backend is forced in turn
    Then the observable contract holds: the reported name matches the concrete type

  Scenario: FR-010 a ring that cannot open falls back to epoll
    Given the feature specification mandates "FR-010 a ring that cannot open falls back to epoll"
    When the implementation satisfies: io_uring_setup returns ENOSYS and no backend is forced
    Then the observable contract holds: the engine is epoll and the run completes

  Scenario: FR-008 counters move once per event
    Given the feature specification mandates "FR-008 counters move once per event"
    When the implementation satisfies: an op completes, whether by success, failure or cancellation
    Then the observable contract holds: each counter moved exactly once for that op

  Scenario: FR-009 a drain stops at the turn deadline
    Given the feature specification mandates "FR-009 a drain stops at the turn deadline"
    When the implementation satisfies: 5000 tasks are queued and the deadline passes mid-drain
    Then the observable contract holds: the drain returns and the rest keep their order
