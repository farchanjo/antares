Feature: Kubernetes API client
  As the desktop app
  I want one typed port for the API server
  So that no call site builds a path or parses a Status by hand

  Scenario: FR-001 core group path
    Given a group version resource with an empty group
    When the path is built for the pods resource
    Then the path starts with "/api/v1"

  Scenario: FR-001 named group path
    Given a group version resource in the apps group
    When the path is built for the deployments resource
    Then the path starts with "/apis/apps/v1"

  Scenario: FR-001 namespace segment
    Given a namespaced resource and a namespace
    When the path is built
    Then the namespace segment sits before the resource

  Scenario: FR-001 subresource segment
    Given a resource with a name
    When the path is built for the status subresource
    Then the path ends with "/status"

  Scenario: FR-002 list options
    Given a label selector and a limit
    When the query is rendered
    Then both appear as query parameters

  Scenario: FR-002 unset options
    Given no option is set
    When the query is rendered
    Then the query is empty

  Scenario: FR-003 patch content type
    Given an apply patch
    When the request headers are built
    Then the content type is the apply patch type

  Scenario: FR-005 not found status
    Given a 404 response carrying a Status with reason NotFound
    When the response is mapped
    Then the error is notFound

  Scenario: FR-005 unexpected body
    Given a 500 response whose body is not a Status
    When the response is mapped
    Then the error is unexpected

  Scenario: FR-004 bookmark does not emit
    Given a watch stream
    When a BOOKMARK event arrives
    Then the stored resource version advances and nothing is emitted

  Scenario: FR-004 expired stream
    Given a watch stream
    When an ERROR event carrying 410 arrives
    Then the stream ends with the expired error

  Scenario: FR-006 kubeconfig current context
    Given a kubeconfig with a current context
    When the document is parsed
    Then the current context is selected

  Scenario: FR-006 missing context
    Given a context naming an absent cluster
    When the document is resolved
    Then the error is unknownCluster

  Scenario: FR-007 bearer credential
    Given a user with a token
    When the credential is rendered
    Then the authorization header carries the bearer scheme

  Scenario: FR-008 log lines arrive as they do
    Given a pod whose log is being followed
    When the container writes a line
    Then the line reaches the caller before the stream ends

  Scenario: FR-008 log options are rendered
    Given a log request naming a container and a tail count
    When the query is rendered
    Then both appear as query parameters

  Scenario: FR-008 cancel ends the stream
    Given a followed log stream
    When the caller cancels the ticket
    Then no line is delivered afterwards

  Scenario: FR-009 exec session carries the three channels
    Given an exec request for a container with a TTY
    When the session opens
    Then stdout and stderr are readable and stdin is writable

  Scenario: FR-009 resize reaches the peer
    Given an open exec session
    When the caller resizes to a new column and row count
    Then one resize frame is sent with both numbers

  Scenario: FR-009 channel order is fixed
    Given an exec session
    When a frame arrives on the error channel
    Then it is reported apart from stdout and stderr

  Scenario: FR-010 discovery parses resources
    Given a discovery document listing a namespaced resource
    When the document is read
    Then the resource is namespaced and its verbs are kept

  Scenario: FR-010 cluster scoped resource
    Given a discovery document listing a cluster scoped resource
    When the document is read
    Then the resource is not namespaced

  Scenario: FR-011 scale reads replicas
    Given a workload with a scale subresource
    When the scale is read
    Then replicas and the observed selector are returned

  Scenario: FR-011 scale writes replicas
    Given a workload and a new replica count
    When the scale is written
    Then the request targets the scale subresource with that count

  Scenario: FR-012 read retries on a retryable status
    Given a read answered with 429 and a Retry-After
    When the client retries
    Then the retry waits the advertised delay

  Scenario: FR-012 write retries only when marked idempotent
    Given a write answered with 503
    When the caller did not mark it idempotent
    Then the write is not retried

  Scenario: FR-012 retry is bounded
    Given a read answered with 503 every time
    When the configured attempt count is spent
    Then the last status is returned as the error
