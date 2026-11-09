Feature: Http Client H2
  The client speaks h2 over TLS, on top of the IoEngine, and never blocks it.

  @planned
  Scenario: FR-001 transport runs on the engine
    Given a client built on an engine
    When  a request is submitted
    Then  every socket and TLS operation goes through the engine
    And   the client owns no thread

  @planned
  Scenario: FR-002 the engine thread never blocks
    Given a completion callback on the engine thread
    When  it submits another operation and starts another request
    Then  both return without waiting
    And   the loop keeps dispatching

  @planned
  Scenario: FR-003 connection establishment
    Given a host that accepts connections
    When  connect, handshake and protocol selection complete
    Then  the connection is ready
    And   a failure at each stage releases the slot with its own error

  @planned
  Scenario: FR-004 http/2
    Given a server that negotiates h2 through ALPN
    When  several requests are in flight
    Then  they run as concurrent streams on one connection
    And   flow control is respected per stream and per connection

  @planned
  Scenario: FR-005 http/1.1 fallback
    Given a server that negotiates http/1.1
    When  a request is submitted
    Then  it completes over HTTP/1.1 with keep-alive
    And   the caller did not choose the version

  @planned
  Scenario: FR-006 redirects
    Given a 303 response carrying a Location
    When  the client follows it
    Then  the follow-up is a GET with no body
    And   a cross-origin hop drops Authorization
    And   exceeding the maximum is an error naming the last location

  @planned
  Scenario: FR-007 async api
    Given an async request
    When  its handle is cancelled
    Then  the completion fires once with cancelled
    And   the slot is released

  @planned
  Scenario: FR-008 sync api
    Given a sync call made from inside a completion callback
    When  it waits
    Then  it returns reentrant instead of deadlocking

  @planned
  Scenario: FR-009 tls verification
    Given a certificate that does not match the host
    When  the handshake completes
    Then  the request fails with a typed verification error
    And   the OpenSSL reason is carried

  @planned
  Scenario: FR-010 bounded memory
    Given the configured ceilings
    When  a response exceeds the header or body ceiling
    Then  the request fails with a typed error
    And   no unbounded allocation happened

  @planned
  Scenario: FR-011 pool configuration
    Given a per-host idle ceiling
    When  one more connection goes idle
    Then  the least recently used idle connection is closed
    And   the request is not refused

  @planned @security
  Scenario: FR-012 timeouts
    Given a read deadline
    When  the peer stops sending
    Then  the request fails with timeout
    And   no thread waits on it

  @planned @security
  Scenario: FR-013 authentication
    Given a bearer credential in the configuration
    When  a request is built
    Then  the Authorization header carries it
    And   the token is absent from the log record

  @planned
  Scenario: FR-014 one published configuration surface
    Given a client built from a configuration
    When  the caller reads config back
    Then  pool, timeouts and auth match what was set
    And   no environment variable was consulted

  @planned
  Scenario: FR-015 retry with backoff
    Given a server answering 503 twice then 200
    When  a GET is sent
    Then  it succeeds on the third attempt
    And   the retry count is two
    And   a POST with the same history is not retried

  @planned
  Scenario: FR-016 resolution stays off the hot path
    Given a URL with a literal address
    When  the connection is opened
    Then  the address is parsed in-process
    And   the resolver is not consulted

  @planned
  Scenario: FR-017 no test sleeps on a clock
    Given the whole suite
    When  a case waits for a completion
    Then  it blocks on the waiter
    And   no case calls a sleep

  @planned
  Scenario: FR-018 a dedicated pool of loops
    Given a pool of two loops
    When  two requests are in flight at once
    Then  each runs on its own loop
    And   a reused connection stays on the loop that owns it

  @planned
  Scenario: FR-019 ALPN decides, h1 is the floor
    Given a server that offers h2
    When  TLS completes
    Then  h2 is selected
    And   a server without h2 answers on http/1.1

  @planned
  Scenario: FR-020 multiplexing
    Given one h2 connection
    When  two requests are in flight
    Then  each has its own stream
    And   a GOAWAY marks the unprocessed streams retryable

  @planned
  Scenario: FR-021 streaming, SSE and WebSocket
    Given a streaming response
    When  bytes arrive
    Then  the handler receives them before the body ends

  @planned
  Scenario: FR-022 chunks are delivered, not buffered
    Given a chunked response
    When  it streams
    Then  the head arrives once
    And   each chunk reaches the sink separately
    And   a silent stream still times out

  @planned
  Scenario: FR-024 the upgrade is the transport change
    Given a WebSocket handshake
    When  the server answers 101
    Then  the head is consumed before any frame
    And   frames after it reach the codec

  @planned
  Scenario: FR-025 every port method is exercised
    Given the client port
    When  the suite runs
    Then  every virtual has a case that reaches it
    And   a stream keeps its sink across a redirect and a retry
