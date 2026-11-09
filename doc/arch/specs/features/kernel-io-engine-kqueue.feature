Feature: Kernel Io Engine Kqueue
  As the transport layer
  I want one completion-oriented interface over the kernel primitive
  So that the HTTP client above never learns a platform

  # One scenario per FR of spec.md, named with the FR id so the link is
  # machine-checkable. All steps carry @planned until the backend binds them.

  @planned
  Scenario: FR-001 completion-oriented port
    Given an IoEngine built from the factory
    When  a read is submitted on a connected descriptor
    Then  submitRead returns before the read completes
    And   the completion callback carries the bytes

  @planned
  Scenario: FR-002 buffer ownership
    Given a buffer moved into submitRead
    When  the caller scope exits before completion
    Then  the callback still receives a valid filled buffer

  @planned
  Scenario: FR-003 kqueue backend
    Given the engine on Darwin
    When  a thousand descriptors are registered
    Then  each registration costs one kevent call
    And   a readiness sweep visits only ready descriptors

  @planned
  Scenario: FR-004 backend selection
    Given the engine factory on Darwin
    When  it builds the engine
    Then  name returns kqueue
    And   no call site names a backend

  @planned
  Scenario: FR-005 typed failure
    Given a descriptor closed behind the engine
    When  a read is submitted on it
    Then  the result is an IoError case
    And   no exception crosses the port

  @planned
  Scenario: FR-006 cancellation
    Given a pending operation
    When  it is cancelled
    Then  the callback fires exactly once with cancelled
    And   it never fires again

  @planned
  Scenario: FR-007 single engine thread
    Given a running engine
    When  post queues a callable from the caller thread
    Then  the callable runs on the engine thread
    And   the caller thread does not run it

  @planned
  Scenario: FR-008 conformance suite
    Given the conformance suite
    When  it runs against kqueue and against poll
    Then  both backends pass every case
    And   the suite names no backend

  @planned
  Scenario: FR-009 handle classification
    Given a socket and a regular file adopted through IoHandle
    When  each handle is read through the port
    Then  both use the same call
    And   the caller branches on nothing

  @planned
  Scenario: FR-010 immediate strategy for files
    Given a file handle read through the engine
    When  the loop is idle
    Then  the read completes without a readiness filter
    And   the loop does not spin

  @planned
  Scenario: FR-011 offset discipline
    Given a file of known size
    When  a read is submitted at offset equal to the size
    Then  the completion carries closed
    And   the caller reads it as end-of-file

  @planned
  Scenario: FR-012 idle costs nothing
    Given an engine with nothing pending
    When  it waits
    Then  it blocks in the kernel
    And   no periodic wakeup happens

  @planned
  Scenario: FR-013 the loop thread is identified
    Given a running loop
    When  a sampler inspects the process
    Then  the loop thread carries a name
