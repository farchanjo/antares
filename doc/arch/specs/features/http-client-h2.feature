Feature: Http Client H2
  The client speaks h2 over TLS, on top of the IoEngine, and never blocks it.

  @planned
  Scenario: FR-001 transport runs on the engine
    Given the feature specification mandates "FR-001 transport runs on the engine"
    When the implementation satisfies: a client built on an engine; a request is submitted
    Then the observable contract holds: every socket and TLS operation goes through the engine

  @planned
  Scenario: FR-002 the engine thread never blocks
    Given the feature specification mandates "FR-002 the engine thread never blocks"
    When the implementation satisfies: a completion callback on the engine thread; it submits another operation and starts another request
    Then the observable contract holds: both return without waiting

  @planned
  Scenario: FR-003 connection establishment
    Given the feature specification mandates "FR-003 connection establishment"
    When the implementation satisfies: a host that accepts connections; connect, handshake and protocol selection complete
    Then the observable contract holds: the connection is ready

  @planned
  Scenario: FR-004 http/2
    Given the feature specification mandates "FR-004 http/2"
    When the implementation satisfies: a server that negotiates h2 through ALPN; several requests are in flight
    Then the observable contract holds: they run as concurrent streams on one connection

  @planned
  Scenario: FR-005 http/1.1 fallback
    Given the feature specification mandates "FR-005 http/1.1 fallback"
    When the implementation satisfies: a server that negotiates http/1.1; a request is submitted
    Then the observable contract holds: it completes over HTTP/1.1 with keep-alive

  @planned
  Scenario: FR-006 redirects
    Given the feature specification mandates "FR-006 redirects"
    When the implementation satisfies: a 303 response carrying a Location; the client follows it
    Then the observable contract holds: the follow-up is a GET with no body

  @planned
  Scenario: FR-007 async api
    Given the feature specification mandates "FR-007 async api"
    When the implementation satisfies: an async request; its handle is cancelled
    Then the observable contract holds: the completion fires once with cancelled

  @planned
  Scenario: FR-008 sync api
    Given the feature specification mandates "FR-008 sync api"
    When the implementation satisfies: a sync call made from inside a completion callback; it waits
    Then the observable contract holds: it returns reentrant instead of deadlocking

  @planned
  Scenario: FR-009 tls verification
    Given the feature specification mandates "FR-009 tls verification"
    When the implementation satisfies: a certificate that does not match the host; the handshake completes
    Then the observable contract holds: the request fails with a typed verification error

  @planned
  Scenario: FR-010 bounded memory
    Given the feature specification mandates "FR-010 bounded memory"
    When the implementation satisfies: the configured ceilings; a response exceeds the header or body ceiling
    Then the observable contract holds: the request fails with a typed error

  @planned
  Scenario: FR-011 pool configuration
    Given the feature specification mandates "FR-011 pool configuration"
    When the implementation satisfies: a per-host idle ceiling; one more connection goes idle
    Then the observable contract holds: the least recently used idle connection is closed

  @planned @security
  Scenario: FR-012 timeouts
    Given the feature specification mandates "FR-012 timeouts"
    When the implementation satisfies: a read deadline; the peer stops sending
    Then the observable contract holds: the request fails with timeout

  @planned @security
  Scenario: FR-013 authentication
    Given the feature specification mandates "FR-013 authentication"
    When the implementation satisfies: a bearer credential in the configuration; a request is built
    Then the observable contract holds: the Authorization header carries it

  @planned
  Scenario: FR-014 one published configuration surface
    Given the feature specification mandates "FR-014 one published configuration surface"
    When the implementation satisfies: a client built from a configuration; the caller reads config back
    Then the observable contract holds: pool, timeouts and auth match what was set

  @planned
  Scenario: FR-015 retry with backoff
    Given the feature specification mandates "FR-015 retry with backoff"
    When the implementation satisfies: a server answering 503 twice then 200; a GET is sent
    Then the observable contract holds: it succeeds on the third attempt

  @planned
  Scenario: FR-016 resolution stays off the hot path
    Given the feature specification mandates "FR-016 resolution stays off the hot path"
    When the implementation satisfies: a URL with a literal address; the connection is opened
    Then the observable contract holds: the address is parsed in-process

  @planned
  Scenario: FR-017 no test sleeps on a clock
    Given the feature specification mandates "FR-017 no test sleeps on a clock"
    When the implementation satisfies: the whole suite; a case waits for a completion
    Then the observable contract holds: it blocks on the waiter

  @planned
  Scenario: FR-018 one loop, and the parallelism is elsewhere
    Given the feature specification mandates "FR-018 one loop, and the parallelism is elsewhere"
    When the implementation satisfies: two requests are in flight at once
    Then the observable contract holds: both run on the one loop and connection state takes no mutex

  @planned
  Scenario: FR-019 ALPN decides, h1 is the floor
    Given the feature specification mandates "FR-019 ALPN decides, h1 is the floor"
    When the implementation satisfies: a server that offers h2; TLS completes
    Then the observable contract holds: h2 is selected

  @planned
  Scenario: FR-020 multiplexing
    Given the feature specification mandates "FR-020 multiplexing"
    When the implementation satisfies: one h2 connection; two requests are in flight
    Then the observable contract holds: each has its own stream

  @planned
  Scenario: FR-021 streaming, SSE and WebSocket
    Given the feature specification mandates "FR-021 streaming, SSE and WebSocket"
    When the implementation satisfies: a streaming response; bytes arrive
    Then the observable contract holds: the handler receives them before the body ends

  @planned
  Scenario: FR-022 chunks are delivered, not buffered
    Given the feature specification mandates "FR-022 chunks are delivered, not buffered"
    When the implementation satisfies: a chunked response; it streams
    Then the observable contract holds: the head arrives once

  @planned
  Scenario: FR-024 the upgrade is the transport change
    Given the feature specification mandates "FR-024 the upgrade is the transport change"
    When the implementation satisfies: a WebSocket handshake; the server answers 101
    Then the observable contract holds: the head is consumed before any frame

  @planned
  Scenario: FR-025 every port method is exercised
    Given the feature specification mandates "FR-025 every port method is exercised"
    When the implementation satisfies: the client port; the suite runs
    Then the observable contract holds: every virtual has a case that reaches it

  Scenario: FR-023 server sent events split across chunks
    Given the feature specification mandates "FR-023 server sent events split across chunks"
    When the implementation satisfies: an event stream cut in the middle of a field; the parser sees each chunk
    Then the observable contract holds: one whole event comes out

  Scenario: FR-023 a control payload stays short
    Given the feature specification mandates "FR-023 a control payload stays short"
    When the implementation satisfies: a close frame carrying more than 125 bytes; the codec encodes it
    Then the observable contract holds: the length is rejected

  Scenario: FR-023 a client frame is masked
    Given the feature specification mandates "FR-023 a client frame is masked"
    When the implementation satisfies: a text frame from the client; the codec encodes it
    Then the observable contract holds: the mask bit is set
