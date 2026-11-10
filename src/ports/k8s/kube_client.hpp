#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "domain/k8s/api_error.hpp"
#include "domain/k8s/api_path.hpp"
#include "domain/k8s/discovery.hpp"
#include "domain/k8s/exec_channel.hpp"
#include "domain/k8s/kubeconfig.hpp"
#include "domain/k8s/options.hpp"
#include "domain/k8s/status.hpp"
#include "domain/k8s/watch_event.hpp"
#include "domain/json/value.hpp"

namespace antares::ports::k8s {

/// Live counters, one row per client. Bounded by construction: no namespace
/// name, no resource name, no identifier ever becomes a counter.
struct ClientStats {
    std::uint64_t submitted = 0;
    std::uint64_t succeeded = 0;
    std::uint64_t failed = 0;
    std::uint64_t retried = 0;
    std::uint64_t watchEvents = 0;
    std::uint64_t bookmarks = 0;
    std::uint64_t expiredStreams = 0;
};

/// A value that arrived from the server. `body` is the raw JSON so the caller
/// decodes only the fields it needs; `meta` is the one block every object has.
struct Object {
    domain::json::Value body;
    domain::k8s::ObjectMeta meta;
};

/// One page of a list, plus the token that reaches the next one (FR-002).
struct ListPage {
    std::vector<Object> items;
    std::string continueToken;
    std::string resourceVersion;
};

/// How many objects one stream has produced, and where it stands.
struct WatchSummary {
    std::uint64_t produced = 0;
    std::uint64_t bookmarks = 0;
    std::string lastResourceVersion;
};

/// What the API server reports about itself. Read once per connection, so the
/// UI can name the cluster and the build can refuse a version it cannot speak.
struct ServerVersion {
    std::string major;
    std::string minor;
    std::string gitVersion;
    std::string platform;
};

using VersionResult = std::expected<ServerVersion, domain::k8s::ApiError>;

/// Receives frames as they arrive. `onEvent` runs on the engine thread.
struct WatchHandler {
    std::function<void(const domain::k8s::WatchEvent &)> onEvent;
    /// Called once when the stream ends for a reason worth reporting beyond a
    /// plain close, a 410 in particular (FR-004).
    std::function<void(const domain::k8s::ApiError &)> onFailure;
};

/// The outcome of one call that returns a single object.
using ObjectResult = std::expected<Object, domain::k8s::ApiError>;
using ListResult = std::expected<ListPage, domain::k8s::ApiError>;
using StatusResult = std::expected<void, domain::k8s::ApiError>;

/// Cancels an in-flight call. Idempotent, safe from any thread.
class Ticket {
public:
    Ticket() = default;
    Ticket(const Ticket &) = delete;
    Ticket &operator=(const Ticket &) = delete;
    Ticket(Ticket &&) = delete;
    Ticket &operator=(Ticket &&) = delete;
    virtual ~Ticket() = default;

    virtual void cancel() = 0;
};

/// Everything that only reads. A surface that takes a `Reader &` cannot mutate
/// the cluster even by mistake: the write verbs are not on the type.
///
/// This is the mode the desktop app runs in first, so a bug in a view cannot
/// delete a workload.
class Reader {
public:
    Reader() = default;
    Reader(const Reader &) = delete;
    Reader &operator=(const Reader &) = delete;
    Reader(Reader &&) = delete;
    Reader &operator=(Reader &&) = delete;
    virtual ~Reader() = default;

    [[nodiscard]] virtual const domain::k8s::Cluster &cluster() const noexcept = 0;
    [[nodiscard]] virtual const domain::k8s::Context &context() const noexcept = 0;

    /// Fetch one object by name (FR-003).
    [[nodiscard]] virtual std::expected<std::shared_ptr<Ticket>, domain::k8s::ApiError>
    get(const domain::k8s::ApiPath &path, std::function<void(ObjectResult)> done) = 0;

    /// Fetch one page (FR-002).
    [[nodiscard]] virtual std::expected<std::shared_ptr<Ticket>, domain::k8s::ApiError>
    list(const domain::k8s::ApiPath &path, const domain::k8s::ListOptions &options,
         std::function<void(ListResult)> done) = 0;

    /// Open a watch (FR-004). The returned ticket closes the stream.
    [[nodiscard]] virtual std::expected<std::shared_ptr<Ticket>, domain::k8s::ApiError>
    watch(const domain::k8s::ApiPath &path, const domain::k8s::ListOptions &options,
          WatchHandler handler, std::function<void(StatusResult)> done) = 0;

    /// Stream a container's log lines (FR-008). `onLine` runs per line that
    /// arrives, without buffering the whole log.
    [[nodiscard]] virtual std::expected<std::shared_ptr<Ticket>, domain::k8s::ApiError>
    logs(const domain::k8s::ApiPath &path, const domain::k8s::ListOptions &options,
         std::function<void(std::string_view)> onLine,
         std::function<void(StatusResult)> done) = 0;

    /// Ask the server what it is. The cheapest request that proves the whole
    /// chain worked: name resolution, TLS, trust anchors and the credential.
    [[nodiscard]] virtual std::expected<std::shared_ptr<Ticket>, domain::k8s::ApiError>
    serverVersion(std::function<void(VersionResult)> done) = 0;

    [[nodiscard]] virtual ClientStats stats() const noexcept = 0;
};

/// What to run, and where (FR-009).
struct ExecOptions {
    std::string container;
    std::vector<std::string> command;
    /// Allocate a TTY. A TTY merges stderr into stdout on the server side, so a
    /// caller that wants them apart must leave this off.
    bool tty = false;
};

/// A live exec session. Cancelling closes it once.
///
/// Output arrives on the engine thread; writes and resizes may come from any
/// thread and are queued by the implementation.
class ExecSession : public Ticket {
public:
    using Output = std::function<void(domain::k8s::ExecChannel, std::string_view)>;
    /// Runs once, when the session ends, whatever ended it.
    using Finished = std::function<void(domain::k8s::ApiError)>;

    ExecSession() = default;
    ~ExecSession() override = default;

    /// Send bytes to the process's stdin.
    virtual void write(std::string_view bytes) = 0;

    /// Resize the peer's terminal. Ignored without a TTY.
    virtual void resize(std::uint16_t columns, std::uint16_t rows) = 0;
};

/// The client for one cluster (FR-003). Every method is asynchronous: it
/// submits and returns, and the completion runs on the engine thread.
///
/// Mutating verbs live here and nowhere else. A read-only surface takes a
/// `Reader &` instead, so the compiler enforces what a running app should not
/// do while the UI is being tuned.
class KubeClient : public Reader {
public:
    KubeClient() = default;
    KubeClient(const KubeClient &) = delete;
    KubeClient &operator=(const KubeClient &) = delete;
    KubeClient(KubeClient &&) = delete;
    KubeClient &operator=(KubeClient &&) = delete;
    ~KubeClient() override = default;

    /// Create the object described by `body`.
    [[nodiscard]] virtual std::expected<std::shared_ptr<Ticket>, domain::k8s::ApiError>
    create(const domain::k8s::ApiPath &path, domain::json::Value body,
           std::function<void(ObjectResult)> done) = 0;

    /// Replace the object described by `body`, which must carry its
    /// `resourceVersion` so a concurrent write is refused rather than lost.
    [[nodiscard]] virtual std::expected<std::shared_ptr<Ticket>, domain::k8s::ApiError>
    update(const domain::k8s::ApiPath &path, domain::json::Value body,
           std::function<void(ObjectResult)> done) = 0;

    /// Apply a partial body (FR-003). `managedFields` is only sent on apply.
    [[nodiscard]] virtual std::expected<std::shared_ptr<Ticket>, domain::k8s::ApiError>
    patch(const domain::k8s::ApiPath &path, domain::k8s::PatchType type,
          std::string_view body, std::optional<std::string> fieldManager,
          std::function<void(ObjectResult)> done) = 0;

    /// Delete the addressed object (FR-003).
    [[nodiscard]] virtual std::expected<std::shared_ptr<Ticket>, domain::k8s::ApiError>
    remove(const domain::k8s::ApiPath &path, const domain::k8s::DeleteOptions &options,
           std::function<void(StatusResult)> done) = 0;

    /// Read or write the `scale` subresource (FR-011).
    [[nodiscard]] virtual std::expected<std::shared_ptr<Ticket>, domain::k8s::ApiError>
    scale(const domain::k8s::ApiPath &path, std::optional<std::int32_t> replicas,
          std::function<void(ObjectResult)> done) = 0;

    /// Open a shell in a container (FR-009). The session arrives once the
    /// upgrade is accepted; a refused upgrade is the error.
    [[nodiscard]] virtual std::expected<std::shared_ptr<ExecSession>, domain::k8s::ApiError>
    exec(const domain::k8s::ApiPath &path, const ExecOptions &options, ExecSession::Output onOutput,
         ExecSession::Finished onFinished) = 0;
};

/// Discovers the resources a cluster serves (FR-010). The answer changes on
/// upgrade, so it is a call, not a table.
class Discovery {
public:
    Discovery() = default;
    Discovery(const Discovery &) = delete;
    Discovery &operator=(const Discovery &) = delete;
    Discovery(Discovery &&) = delete;
    Discovery &operator=(Discovery &&) = delete;
    virtual ~Discovery() = default;

    /// One served resource, with what it allows. The shape is the domain's:
    /// parsing a document is domain work, so it is not declared twice.
    using Resource = domain::k8s::ResourceDescriptor;

    /// One served group version.
    using GroupVersion = domain::k8s::GroupVersionDescriptor;

    using ResourceResult = std::expected<std::vector<GroupVersion>, domain::k8s::ApiError>;

    /// List the groups and versions the server serves.
    [[nodiscard]] virtual std::expected<std::shared_ptr<Ticket>, domain::k8s::ApiError>
    apiGroups(std::function<void(ResourceResult)> done) = 0;

    /// List the resources of one group version.
    [[nodiscard]] virtual std::expected<std::shared_ptr<Ticket>, domain::k8s::ApiError>
    serverResources(std::string_view group, std::string_view version,
                    std::function<void(ResourceResult)> done) = 0;
};

/// Resolves the credential of a user (FR-007). Pure: reading a token file or
/// running a plugin is the adapter's job, not this port's.
class CredentialSource {
public:
    CredentialSource() = default;
    CredentialSource(const CredentialSource &) = delete;
    CredentialSource &operator=(const CredentialSource &) = delete;
    CredentialSource(CredentialSource &&) = delete;
    CredentialSource &operator=(CredentialSource &&) = delete;
    virtual ~CredentialSource() = default;

    /// The credential for `user`, already usable: a bearer token, a pair of
    /// PEM documents, or nothing at all.
    [[nodiscard]] virtual std::expected<domain::k8s::User, domain::k8s::ConfigError>
    resolve(const domain::k8s::User &user) = 0;
};

} // namespace antares::ports::k8s
