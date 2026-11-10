Feature: Kubernetes API client
  As the desktop app
  I want one typed port for the API server
  So that no call site builds a path or parses a Status by hand

  Scenario: FR-001 core group path
    Given the feature specification mandates "FR-001 core group path"
    When the implementation satisfies: a group version resource with an empty group; the path is built for the pods resource
    Then the observable contract holds: the path starts with "/api/v1"

  Scenario: FR-001 named group path
    Given the feature specification mandates "FR-001 named group path"
    When the implementation satisfies: a group version resource in the apps group; the path is built for the deployments resource
    Then the observable contract holds: the path starts with "/apis/apps/v1"

  Scenario: FR-001 namespace segment
    Given the feature specification mandates "FR-001 namespace segment"
    When the implementation satisfies: a namespaced resource and a namespace; the path is built
    Then the observable contract holds: the namespace segment sits before the resource

  Scenario: FR-001 subresource segment
    Given the feature specification mandates "FR-001 subresource segment"
    When the implementation satisfies: a resource with a name; the path is built for the status subresource
    Then the observable contract holds: the path ends with "/status"

  Scenario: FR-002 list options
    Given the feature specification mandates "FR-002 list options"
    When the implementation satisfies: a label selector and a limit; the query is rendered
    Then the observable contract holds: both appear as query parameters

  Scenario: FR-002 unset options
    Given the feature specification mandates "FR-002 unset options"
    When the implementation satisfies: no option is set; the query is rendered
    Then the observable contract holds: the query is empty

  Scenario: FR-003 patch content type
    Given the feature specification mandates "FR-003 patch content type"
    When the implementation satisfies: an apply patch; the request headers are built
    Then the observable contract holds: the content type is the apply patch type

  Scenario: FR-005 not found status
    Given the feature specification mandates "FR-005 not found status"
    When the implementation satisfies: a 404 response carrying a Status with reason NotFound; the response is mapped
    Then the observable contract holds: the error is notFound

  Scenario: FR-005 unexpected body
    Given the feature specification mandates "FR-005 unexpected body"
    When the implementation satisfies: a 500 response whose body is not a Status; the response is mapped
    Then the observable contract holds: the error is unexpected

  Scenario: FR-004 bookmark does not emit
    Given the feature specification mandates "FR-004 bookmark does not emit"
    When the implementation satisfies: a watch stream; a BOOKMARK event arrives
    Then the observable contract holds: the stored resource version advances and nothing is emitted

  Scenario: FR-004 expired stream
    Given the feature specification mandates "FR-004 expired stream"
    When the implementation satisfies: a watch stream; an ERROR event carrying 410 arrives
    Then the observable contract holds: the stream ends with the expired error

  Scenario: FR-006 kubeconfig current context
    Given the feature specification mandates "FR-006 kubeconfig current context"
    When the implementation satisfies: a kubeconfig with a current context; the document is parsed
    Then the observable contract holds: the current context is selected

  Scenario: FR-006 missing context
    Given the feature specification mandates "FR-006 missing context"
    When the implementation satisfies: a context naming an absent cluster; the document is resolved
    Then the observable contract holds: the error is unknownCluster

  Scenario: FR-007 bearer credential
    Given the feature specification mandates "FR-007 bearer credential"
    When the implementation satisfies: a user with a token; the credential is rendered
    Then the observable contract holds: the authorization header carries the bearer scheme

  Scenario: FR-008 log lines arrive as they do
    Given the feature specification mandates "FR-008 log lines arrive as they do"
    When the implementation satisfies: a pod whose log is being followed; the container writes a line
    Then the observable contract holds: the line reaches the caller before the stream ends

  Scenario: FR-008 log options are rendered
    Given the feature specification mandates "FR-008 log options are rendered"
    When the implementation satisfies: a log request naming a container and a tail count; the query is rendered
    Then the observable contract holds: both appear as query parameters

  Scenario: FR-008 cancel ends the stream
    Given the feature specification mandates "FR-008 cancel ends the stream"
    When the implementation satisfies: a followed log stream; the caller cancels the ticket
    Then the observable contract holds: no line is delivered afterwards

  Scenario: FR-009 exec session carries the three channels
    Given the feature specification mandates "FR-009 exec session carries the three channels"
    When the implementation satisfies: an exec request for a container with a TTY; the session opens
    Then the observable contract holds: stdout and stderr are readable and stdin is writable

  Scenario: FR-009 resize reaches the peer
    Given the feature specification mandates "FR-009 resize reaches the peer"
    When the implementation satisfies: an open exec session; the caller resizes to a new column and row count
    Then the observable contract holds: one resize frame is sent with both numbers

  Scenario: FR-009 channel order is fixed
    Given the feature specification mandates "FR-009 channel order is fixed"
    When the implementation satisfies: an exec session; a frame arrives on the error channel
    Then the observable contract holds: it is reported apart from stdout and stderr

  Scenario: FR-010 discovery parses resources
    Given the feature specification mandates "FR-010 discovery parses resources"
    When the implementation satisfies: a discovery document listing a namespaced resource; the document is read
    Then the observable contract holds: the resource is namespaced and its verbs are kept

  Scenario: FR-010 cluster scoped resource
    Given the feature specification mandates "FR-010 cluster scoped resource"
    When the implementation satisfies: a discovery document listing a cluster scoped resource; the document is read
    Then the observable contract holds: the resource is not namespaced

  Scenario: FR-011 scale reads replicas
    Given the feature specification mandates "FR-011 scale reads replicas"
    When the implementation satisfies: a workload with a scale subresource; the scale is read
    Then the observable contract holds: replicas and the observed selector are returned

  Scenario: FR-011 scale writes replicas
    Given the feature specification mandates "FR-011 scale writes replicas"
    When the implementation satisfies: a workload and a new replica count; the scale is written
    Then the observable contract holds: the request targets the scale subresource with that count

  Scenario: FR-012 read retries on a retryable status
    Given the feature specification mandates "FR-012 read retries on a retryable status"
    When the implementation satisfies: a read answered with 429 and a Retry-After; the client retries
    Then the observable contract holds: the retry waits the advertised delay

  Scenario: FR-012 write retries only when marked idempotent
    Given the feature specification mandates "FR-012 write retries only when marked idempotent"
    When the implementation satisfies: a write answered with 503; the caller did not mark it idempotent
    Then the observable contract holds: the write is not retried

  Scenario: FR-012 retry is bounded
    Given the feature specification mandates "FR-012 retry is bounded"
    When the implementation satisfies: a read answered with 503 every time; the configured attempt count is spent
    Then the observable contract holds: the last status is returned as the error
