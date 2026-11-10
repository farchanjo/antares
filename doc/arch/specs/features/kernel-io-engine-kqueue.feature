Feature: Kernel Io Engine Kqueue
  As the transport layer
  I want one completion-oriented interface over the kernel primitive
  So that the HTTP client above never learns a platform

  # One scenario per FR of spec.md, named with the FR id so the link is
  # machine-checkable. All steps carry @planned until the backend binds them.

  @planned
  Scenario: FR-001 completion-oriented port
    Given the feature specification mandates "FR-001 completion-oriented port"
    When the implementation satisfies: an IoEngine built from the factory; a read is submitted on a connected descriptor
    Then the observable contract holds: submitRead returns before the read completes

  @planned
  Scenario: FR-002 buffer ownership
    Given the feature specification mandates "FR-002 buffer ownership"
    When the implementation satisfies: a buffer moved into submitRead; the caller scope exits before completion
    Then the observable contract holds: the callback still receives a valid filled buffer

  @planned
  Scenario: FR-003 kqueue backend
    Given the feature specification mandates "FR-003 kqueue backend"
    When the implementation satisfies: the engine on Darwin; a thousand descriptors are registered
    Then the observable contract holds: each registration costs one kevent call

  @planned
  Scenario: FR-004 backend selection
    Given the feature specification mandates "FR-004 backend selection"
    When the implementation satisfies: the engine factory on Darwin; it builds the engine
    Then the observable contract holds: name returns kqueue

  @planned
  Scenario: FR-005 typed failure
    Given the feature specification mandates "FR-005 typed failure"
    When the implementation satisfies: a descriptor closed behind the engine; a read is submitted on it
    Then the observable contract holds: the result is an IoError case

  @planned
  Scenario: FR-006 cancellation
    Given the feature specification mandates "FR-006 cancellation"
    When the implementation satisfies: a pending operation; it is cancelled
    Then the observable contract holds: the callback fires exactly once with cancelled

  @planned
  Scenario: FR-007 single engine thread
    Given the feature specification mandates "FR-007 single engine thread"
    When the implementation satisfies: a running engine; post queues a callable from the caller thread
    Then the observable contract holds: the callable runs on the engine thread

  @planned
  Scenario: FR-008 conformance suite
    Given the feature specification mandates "FR-008 conformance suite"
    When the implementation satisfies: the conformance suite; it runs against kqueue and against poll
    Then the observable contract holds: both backends pass every case

  @planned
  Scenario: FR-009 handle classification
    Given the feature specification mandates "FR-009 handle classification"
    When the implementation satisfies: a socket and a regular file adopted through IoHandle; each handle is read through the port
    Then the observable contract holds: both use the same call

  @planned
  Scenario: FR-010 immediate strategy for files
    Given the feature specification mandates "FR-010 immediate strategy for files"
    When the implementation satisfies: a file handle read through the engine; the loop is idle
    Then the observable contract holds: the read completes without a readiness filter

  @planned
  Scenario: FR-011 offset discipline
    Given the feature specification mandates "FR-011 offset discipline"
    When the implementation satisfies: a file of known size; a read is submitted at offset equal to the size
    Then the observable contract holds: the completion carries closed

  @planned
  Scenario: FR-012 idle costs nothing
    Given the feature specification mandates "FR-012 idle costs nothing"
    When the implementation satisfies: an engine with nothing pending; it waits
    Then the observable contract holds: it blocks in the kernel

  @planned
  Scenario: FR-013 the loop thread is identified
    Given the feature specification mandates "FR-013 the loop thread is identified"
    When the implementation satisfies: a running loop; a sampler inspects the process
    Then the observable contract holds: the loop thread carries a name
