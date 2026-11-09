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
