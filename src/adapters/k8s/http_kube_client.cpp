#include "adapters/k8s/http_kube_client.hpp"

#include <expected>
#include <optional>
#include <string_view>
#include <utility>

#include "domain/json/value.hpp"
#include "domain/k8s/entry_decoder.hpp"

namespace antares::adapters::k8s {

namespace {

using domain::k8s::ApiError;
using domain::k8s::Reason;

constexpr std::string_view JSON_CONTENT_TYPE = "application/json";
constexpr std::string_view ACCEPT_TYPE = "application/json";

/// The request body as bytes. A kube request is little, so the copy costs less
/// than the bookkeeping a view would need.
[[nodiscard]] domain::http::Body toBody(std::string_view text)
{
    domain::http::Body body;
    body.reserve(text.size());
    for (const char value : text) {
        body.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    }
    return body;
}

[[nodiscard]] std::string_view toText(const domain::http::Body &body)
{
    return {reinterpret_cast<const char *>(body.data()), body.size()};
}

/// A failure that never reached the server.
[[nodiscard]] ApiError transportFailure(domain::http::HttpError error)
{
    return ApiError{Reason::unexpected, 0, std::string{domain::http::describe(error)}};
}

[[nodiscard]] ports::k8s::Object decodeObject(const domain::http::Response &response)
{
    ports::k8s::Object object;
    const auto document = domain::json::Value::parse(toText(response.body));
    if (document) {
        object.body = *document;
        object.meta = domain::k8s::ObjectMeta::fromJson(object.body);
    }
    return object;
}

/// Forward a completion onto the caller's thread, keeping the functor alive
/// until it fires. A captured value that dies before the completion runs is the
/// classic use-after-free here, so the holder takes ownership.
class TicketHandle final : public ports::k8s::Ticket {
public:
    explicit TicketHandle(std::shared_ptr<ports::http::Ticket> inner) : inner_(std::move(inner))
    {
    }

    void cancel() override
    {
        if (inner_) {
            inner_->cancel();
        }
    }

private:
    std::shared_ptr<ports::http::Ticket> inner_;
};

} // namespace

HttpKubeClient::HttpKubeClient(Binding binding, std::shared_ptr<ports::http::Client> http)
    : binding_(std::move(binding)), http_(std::move(http))
{
}

std::shared_ptr<HttpKubeClient> HttpKubeClient::create(Binding binding,
                                                       std::shared_ptr<ports::http::Client> http)
{
    return std::shared_ptr<HttpKubeClient>(
        new HttpKubeClient(std::move(binding), std::move(http)));
}

const domain::k8s::Cluster &HttpKubeClient::cluster() const noexcept
{
    return binding_.cluster;
}

const domain::k8s::Context &HttpKubeClient::context() const noexcept
{
    return binding_.context;
}

domain::http::Request HttpKubeClient::request(domain::http::Method method, std::string_view path,
                                              std::string_view query) const
{
    domain::http::Request built;
    built.method = method;
    built.url = binding_.base + std::string{path};
    if (!query.empty()) {
        built.url += '?';
        built.url += query;
    }
    built.headers.push_back(domain::http::Header{"accept", std::string{ACCEPT_TYPE}});

    if (!binding_.user.token.empty()) {
        built.headers.push_back(
            domain::http::Header{"authorization", "Bearer " + binding_.user.token});
    }
    return built;
}

std::expected<std::shared_ptr<ports::k8s::Ticket>, ApiError>
HttpKubeClient::send(domain::http::Request built,
                     std::function<void(ports::k8s::ObjectResult)> done)
{
    ++stats_.submitted;
    const auto weak = weak_from_this();

    auto completion = [weak, done = std::move(done)](
                          std::expected<domain::http::Response, domain::http::HttpError> result) {
        const auto self = weak.lock();
        if (self && result && result->status >= 200 && result->status < 300) {
            ++self->stats_.succeeded;
            done(ports::k8s::ObjectResult{decodeObject(*result)});
            return;
        }
        if (self) {
            ++self->stats_.failed;
        }
        const ApiError failure = result ? domain::k8s::mapFailure(result->status,
                                                                  toText(result->body))
                                        : transportFailure(result.error());
        done(ports::k8s::ObjectResult{std::unexpected(failure)});
    };

    auto ticket = http_->submit(std::move(built), std::move(completion));
    if (!ticket) {
        --stats_.submitted;
        return std::unexpected(transportFailure(ticket.error()));
    }
    return std::shared_ptr<ports::k8s::Ticket>{new TicketHandle(std::move(*ticket))};
}

std::expected<std::shared_ptr<ports::k8s::Ticket>, ApiError>
HttpKubeClient::get(const domain::k8s::ApiPath &path,
                    std::function<void(ports::k8s::ObjectResult)> done)
{
    return send(request(domain::http::Method::get, path.render(), {}), std::move(done));
}

std::expected<std::shared_ptr<ports::k8s::Ticket>, ApiError>
HttpKubeClient::list(const domain::k8s::ApiPath &path, const domain::k8s::ListOptions &options,
                     std::function<void(ports::k8s::ListResult)> done)
{
    ++stats_.submitted;
    const auto weak = weak_from_this();

    auto completion = [weak, done = std::move(done)](
                          std::expected<domain::http::Response, domain::http::HttpError> result) {
        const auto self = weak.lock();
        if (self) {
            ++self->stats_.pagesFetched;
        }
        if (!result) {
            if (self) {
                ++self->stats_.failed;
            }
            done(ports::k8s::ListResult{std::unexpected(transportFailure(result.error()))});
            return;
        }
        if (result->status < 200 || result->status >= 300) {
            if (self) {
                ++self->stats_.failed;
            }
            done(ports::k8s::ListResult{
                std::unexpected(domain::k8s::mapFailure(result->status, toText(result->body)))});
            return;
        }

        if (self) {
            ++self->stats_.succeeded;
        }
        ports::k8s::ListPage page;
        const auto document = domain::json::Value::parse(toText(result->body));
        if (document) {
            page.continueToken = std::string{document->member("metadata").textAt("continue")};
            page.resourceVersion =
                std::string{document->member("metadata").textAt("resourceVersion")};
            // The document dies with this completion; only the projection
            // survives, so the page is not a second tree (FR-005).
            for (const domain::json::Value &item : document->member("items").items()) {
                auto entry = domain::k8s::decodeEntry(item);
                if (entry) {
                    page.items.push_back(std::move(*entry));
                }
            }
        }
        done(ports::k8s::ListResult{std::move(page)});
    };

    auto ticket = http_->submit(request(domain::http::Method::get, path.render(), options.render()),
                               std::move(completion));
    if (!ticket) {
        --stats_.submitted;
        return std::unexpected(transportFailure(ticket.error()));
    }
    return std::shared_ptr<ports::k8s::Ticket>{new TicketHandle(std::move(*ticket))};
}

std::expected<std::shared_ptr<ports::k8s::Ticket>, ApiError>
HttpKubeClient::create(const domain::k8s::ApiPath &path, domain::json::Value body,
                       std::function<void(ports::k8s::ObjectResult)> done)
{
    auto built = request(domain::http::Method::post, path.render(), {});
    built.headers.push_back(
        domain::http::Header{"content-type", std::string{JSON_CONTENT_TYPE}});
    built.body = toBody(body.dump());
    return send(std::move(built), std::move(done));
}

std::expected<std::shared_ptr<ports::k8s::Ticket>, ApiError>
HttpKubeClient::update(const domain::k8s::ApiPath &path, domain::json::Value body,
                       std::function<void(ports::k8s::ObjectResult)> done)
{
    auto built = request(domain::http::Method::put, path.render(), {});
    built.headers.push_back(
        domain::http::Header{"content-type", std::string{JSON_CONTENT_TYPE}});
    built.body = toBody(body.dump());
    return send(std::move(built), std::move(done));
}

std::expected<std::shared_ptr<ports::k8s::Ticket>, ApiError>
HttpKubeClient::patch(const domain::k8s::ApiPath &path, domain::k8s::PatchType type,
                      std::string_view body, std::optional<std::string> fieldManager,
                      std::function<void(ports::k8s::ObjectResult)> done)
{
    auto built = request(domain::http::Method::patch, path.render(), {});
    built.headers.push_back(
        domain::http::Header{"content-type", std::string{domain::k8s::patchContentType(type)}});
    if (fieldManager) {
        built.headers.push_back(
            domain::http::Header{"field-manager", std::move(*fieldManager)});
    }
    built.body = toBody(body);
    return send(std::move(built), std::move(done));
}

std::expected<std::shared_ptr<ports::k8s::Ticket>, ApiError>
HttpKubeClient::remove(const domain::k8s::ApiPath &path,
                       const domain::k8s::DeleteOptions &options,
                       std::function<void(ports::k8s::StatusResult)> done)
{
    ++stats_.submitted;
    const auto weak = weak_from_this();

    auto completion = [weak, done = std::move(done)](
                          std::expected<domain::http::Response, domain::http::HttpError> result) {
        const auto self = weak.lock();
        if (!result) {
            if (self) {
                ++self->stats_.failed;
            }
            done(ports::k8s::StatusResult{std::unexpected(transportFailure(result.error()))});
            return;
        }
        if (result->status >= 200 && result->status < 300) {
            if (self) {
                ++self->stats_.succeeded;
            }
            done(ports::k8s::StatusResult{});
            return;
        }
        if (self) {
            ++self->stats_.failed;
        }
        done(ports::k8s::StatusResult{
            std::unexpected(domain::k8s::mapFailure(result->status, toText(result->body)))});
    };

    auto built = request(domain::http::Method::del, path.render(), {});
    built.headers.push_back(
        domain::http::Header{"content-type", std::string{JSON_CONTENT_TYPE}});
    built.body = toBody(options.render());

    auto ticket = http_->submit(std::move(built), std::move(completion));
    if (!ticket) {
        --stats_.submitted;
        return std::unexpected(transportFailure(ticket.error()));
    }
    return std::shared_ptr<ports::k8s::Ticket>{new TicketHandle(std::move(*ticket))};
}

std::expected<std::shared_ptr<ports::k8s::Ticket>, ApiError>
HttpKubeClient::watch(const domain::k8s::ApiPath &path,
                      const domain::k8s::ListOptions &options,
                      ports::k8s::WatchHandler handler,
                      std::function<void(ports::k8s::StatusResult)> done)
{
    domain::k8s::ListOptions watching = options;
    watching.watch = true;

    // Frames arrive as body chunks that do not align to lines, so the tail is
    // carried between chunks (FR-004). The cursor reads frames in place and
    // compacts once per chunk, so nothing is copied out of the accumulator
    // (FR-005).
    auto buffer = std::make_shared<std::string>();
    auto sharedHandler = std::make_shared<ports::k8s::WatchHandler>(std::move(handler));
    const auto weak = weak_from_this();

    ports::http::StreamHandler streaming;
    streaming.onChunk = [buffer, sharedHandler, weak](std::span<const std::byte> chunk) {
        buffer->append(reinterpret_cast<const char *>(chunk.data()), chunk.size());

        std::size_t compacted = 0;
        {
            domain::k8s::FrameCursor cursor{*buffer};
            while (const std::optional<std::string_view> frame = cursor.next()) {
                auto event = domain::k8s::decodeWatchEvent(*frame);
                if (!event || !*event) {
                    continue;
                }
                const auto self = weak.lock();
                if (self) {
                    ++self->stats_.framesParsed;
                    if ((**event).type == domain::k8s::EventType::bookmark) {
                        ++self->stats_.bookmarks;
                    } else {
                        ++self->stats_.watchEvents;
                    }
                }
                if (sharedHandler->onEvent) {
                    sharedHandler->onEvent(**event);
                }
            }
            compacted = cursor.compact();
        }
        const auto self = weak.lock();
        if (self && compacted > 0) {
            ++self->stats_.bufferCompactions;
        }
    };

    auto completion = [weak, sharedHandler, done = std::move(done)](
                          std::expected<domain::http::Response, domain::http::HttpError> result) {
        const auto self = weak.lock();
        if (!result) {
            if (self) {
                ++self->stats_.failed;
            }
            const ApiError failure = transportFailure(result.error());
            if (sharedHandler->onFailure) {
                sharedHandler->onFailure(failure);
            }
            done(ports::k8s::StatusResult{std::unexpected(failure)});
            return;
        }
        if (result->status >= 200 && result->status < 300) {
            if (self) {
                ++self->stats_.succeeded;
            }
            done(ports::k8s::StatusResult{});
            return;
        }
        const ApiError failure = domain::k8s::mapFailure(result->status, toText(result->body));
        if (self) {
            ++self->stats_.failed;
            if (failure.reason == Reason::expired) {
                ++self->stats_.expiredStreams;
            }
        }
        if (sharedHandler->onFailure) {
            sharedHandler->onFailure(failure);
        }
        done(ports::k8s::StatusResult{std::unexpected(failure)});
    };

    ++stats_.submitted;
    auto ticket = http_->submitStreaming(request(domain::http::Method::get, path.render(),
                                                 watching.render()),
                                         std::move(streaming), std::move(completion));
    if (!ticket) {
        --stats_.submitted;
        return std::unexpected(transportFailure(ticket.error()));
    }
    return std::shared_ptr<ports::k8s::Ticket>{new TicketHandle(std::move(*ticket))};
}

std::expected<std::shared_ptr<ports::k8s::Ticket>, ApiError>
HttpKubeClient::logs(const domain::k8s::ApiPath &path,
                     const domain::k8s::ListOptions &options,
                     std::function<void(std::string_view)> onLine,
                     std::function<void(ports::k8s::StatusResult)> done)
{
    auto buffer = std::make_shared<std::string>();
    auto sink = std::make_shared<std::function<void(std::string_view)>>(std::move(onLine));

    ports::http::StreamHandler streaming;
    streaming.onChunk = [buffer, sink](std::span<const std::byte> chunk) {
        buffer->append(reinterpret_cast<const char *>(chunk.data()), chunk.size());
        // The cursor views the accumulator and compacts on destruction, once
        // per chunk (FR-005).
        domain::k8s::FrameCursor cursor{*buffer};
        while (const std::optional<std::string_view> line = cursor.next()) {
            (*sink)(*line);
        }
    };

    auto completion = [done = std::move(done)](
                          std::expected<domain::http::Response, domain::http::HttpError> result) {
        if (!result) {
            done(ports::k8s::StatusResult{std::unexpected(transportFailure(result.error()))});
            return;
        }
        if (result->status >= 200 && result->status < 300) {
            done(ports::k8s::StatusResult{});
            return;
        }
        done(ports::k8s::StatusResult{
            std::unexpected(domain::k8s::mapFailure(result->status, toText(result->body)))});
    };

    ++stats_.submitted;
    auto ticket = http_->submitStreaming(
        request(domain::http::Method::get,
                path.with(domain::k8s::Subresource::log).render(), options.render()),
        std::move(streaming),
        std::move(completion));
    if (!ticket) {
        --stats_.submitted;
        return std::unexpected(transportFailure(ticket.error()));
    }
    return std::shared_ptr<ports::k8s::Ticket>{new TicketHandle(std::move(*ticket))};
}

std::expected<std::shared_ptr<ports::k8s::Ticket>, ApiError>
HttpKubeClient::scale(const domain::k8s::ApiPath &path, std::optional<std::int32_t> replicas,
                      std::function<void(ports::k8s::ObjectResult)> done)
{
    const domain::k8s::ApiPath target = path.with(domain::k8s::Subresource::scale);

    if (!replicas) {
        return get(target, std::move(done));
    }

    // A scale write is a merge patch carrying only the replica count, so a
    // concurrent change to the selector is not overwritten.
    std::string body = R"({"spec":{"replicas":)";
    body += std::to_string(*replicas);
    body += "}}";

    auto built = request(domain::http::Method::patch, target.render(), {});
    built.headers.push_back(domain::http::Header{
        "content-type", std::string{domain::k8s::patchContentType(domain::k8s::PatchType::merge)}});
    built.body = toBody(body);
    return send(std::move(built), std::move(done));
}

std::expected<std::shared_ptr<ports::k8s::Ticket>, ApiError>
HttpKubeClient::serverVersion(std::function<void(ports::k8s::VersionResult)> done)
{
    ++stats_.submitted;
    const auto weak = weak_from_this();

    auto completion = [weak, done = std::move(done)](
                          std::expected<domain::http::Response, domain::http::HttpError> result) {
        const auto self = weak.lock();
        if (!result) {
            if (self) {
                ++self->stats_.failed;
            }
            done(ports::k8s::VersionResult{std::unexpected(transportFailure(result.error()))});
            return;
        }
        if (result->status < 200 || result->status >= 300) {
            if (self) {
                ++self->stats_.failed;
            }
            done(ports::k8s::VersionResult{
                std::unexpected(domain::k8s::mapFailure(result->status, toText(result->body)))});
            return;
        }
        if (self) {
            ++self->stats_.succeeded;
        }

        ports::k8s::ServerVersion version;
        const auto document = domain::json::Value::parse(toText(result->body));
        if (document) {
            version.major = std::string{document->textAt("major")};
            version.minor = std::string{document->textAt("minor")};
            version.gitVersion = std::string{document->textAt("gitVersion")};
            version.platform = std::string{document->textAt("platform")};
        }
        done(ports::k8s::VersionResult{std::move(version)});
    };

    auto ticket = http_->submit(request(domain::http::Method::get, "/version", {}),
                                std::move(completion));
    if (!ticket) {
        --stats_.submitted;
        return std::unexpected(transportFailure(ticket.error()));
    }
    return std::shared_ptr<ports::k8s::Ticket>{new TicketHandle(std::move(*ticket))};
}

ports::k8s::KubeStats HttpKubeClient::stats() const noexcept
{
    return stats_;
}


namespace {

/// Collects the two discovery replies onto one answer.
///
/// Both halves arrive on the engine thread, but not necessarily in the same
/// pass, so the state is shared rather than captured by value. The first
/// failure wins and the second reply is dropped: half a cluster is worse than a
/// named error.
struct GroupMerge {
    ports::k8s::Discovery::ResourceResult result{std::vector<ports::k8s::Discovery::GroupVersion>{}};
    std::function<void(ports::k8s::Discovery::ResourceResult)> done;
    int remaining = 2;
    bool settled = false;

    void settle()
    {
        if (settled || --remaining > 0) {
            return;
        }
        settled = true;
        auto out = std::move(done);
        out(std::move(result));
    }
};

/// Cancels every request behind a call that needed more than one.
class MultiTicket final : public ports::k8s::Ticket {
public:
    void adopt(std::shared_ptr<ports::k8s::Ticket> ticket)
    {
        if (ticket) {
            inner_.push_back(std::move(ticket));
        }
    }

    void cancel() override
    {
        for (const auto &ticket : inner_) {
            ticket->cancel();
        }
    }

private:
    std::vector<std::shared_ptr<ports::k8s::Ticket>> inner_;
};

} // namespace

std::expected<std::shared_ptr<ports::k8s::Ticket>, ApiError> HttpKubeClient::apiGroups(
    std::function<void(ports::k8s::Discovery::ResourceResult)> done)
{
    auto merge = std::make_shared<GroupMerge>();
    merge->done = std::move(done);
    auto ticket = std::make_shared<MultiTicket>();
    const auto weak = weak_from_this();

    // `append` differs per half; everything else about the two requests is the
    // same, so the loop below runs once per document instead of twice by hand.
    const auto fetch = [&](std::string_view path,
                           void (*append)(const domain::json::Value &,
                                          std::vector<ports::k8s::Discovery::GroupVersion> &))
        -> std::expected<void, ApiError> {
        auto completion = [weak, merge, append](
                              std::expected<domain::http::Response, domain::http::HttpError> result) {
            const auto self = weak.lock();
            if (!result) {
                if (self) {
                    ++self->stats_.failed;
                }
                merge->result = std::unexpected(transportFailure(result.error()));
                merge->settle();
                return;
            }
            if (result->status < 200 || result->status >= 300) {
                if (self) {
                    ++self->stats_.failed;
                }
                merge->result = std::unexpected(
                    domain::k8s::mapFailure(result->status, toText(result->body)));
                merge->settle();
                return;
            }
            if (self) {
                ++self->stats_.succeeded;
            }
            const auto document = domain::json::Value::parse(toText(result->body));
            if (!document) {
                merge->result = std::unexpected(ApiError{});
                merge->settle();
                return;
            }
            append(*document, *merge->result);
            merge->settle();
        };

        auto submitted =
            http_->submit(request(domain::http::Method::get, path, {}), std::move(completion));
        if (!submitted) {
            return std::unexpected(transportFailure(submitted.error()));
        }
        ticket->adopt(std::shared_ptr<ports::k8s::Ticket>{new TicketHandle(std::move(*submitted))});
        return {};
    };

    ++stats_.submitted;
    if (const auto core = fetch("/api", domain::k8s::appendCoreVersions); !core) {
        --stats_.submitted;
        return std::unexpected(core.error());
    }
    if (const auto groups = fetch("/apis", domain::k8s::appendGroups); !groups) {
        return std::unexpected(groups.error());
    }
    return std::shared_ptr<ports::k8s::Ticket>{std::move(ticket)};
}

std::expected<std::shared_ptr<ports::k8s::Ticket>, ApiError> HttpKubeClient::serverResources(
    std::string_view group, std::string_view version,
    std::function<void(ports::k8s::Discovery::ResourceResult)> done)
{
    if (version.empty()) {
        return std::unexpected(ApiError{});
    }

    // The core group has no name segment; every other group does.
    std::string path{"/api/"};
    if (!group.empty()) {
        path = "/apis/" + std::string{group} + "/";
    }
    path += version;

    ++stats_.submitted;
    const auto weak = weak_from_this();

    auto completion = [weak, done = std::move(done)](
                          std::expected<domain::http::Response, domain::http::HttpError> result) {
        const auto self = weak.lock();
        if (!result) {
            if (self) {
                ++self->stats_.failed;
            }
            done(ports::k8s::Discovery::ResourceResult{std::unexpected(transportFailure(result.error()))});
            return;
        }
        if (result->status < 200 || result->status >= 300) {
            if (self) {
                ++self->stats_.failed;
            }
            done(ports::k8s::Discovery::ResourceResult{
                std::unexpected(domain::k8s::mapFailure(result->status, toText(result->body)))});
            return;
        }
        if (self) {
            ++self->stats_.succeeded;
        }

        const auto document = domain::json::Value::parse(toText(result->body));
        if (!document) {
            done(ports::k8s::Discovery::ResourceResult{std::unexpected(ApiError{})});
            return;
        }

        ports::k8s::Discovery::GroupVersion served =
            domain::k8s::parseResourceList(*document);
        std::vector<ports::k8s::Discovery::GroupVersion> versions;
        versions.push_back(std::move(served));
        done(ports::k8s::Discovery::ResourceResult{std::move(versions)});
    };

    auto ticket = http_->submit(request(domain::http::Method::get, path, {}), std::move(completion));
    if (!ticket) {
        --stats_.submitted;
        return std::unexpected(transportFailure(ticket.error()));
    }
    return std::shared_ptr<ports::k8s::Ticket>{new TicketHandle(std::move(*ticket))};
}

} // namespace antares::adapters::k8s

namespace antares::adapters::k8s {

namespace {

/// An exec session (FR-009), open or still opening.
///
/// Frames carry a channel byte (ADR-0019). The upgrade is asynchronous, so the
/// session exists before the socket does: writes made in between are queued and
/// flushed on attach. That keeps the port synchronous without ever dropping
/// what the caller typed.
class ExecConnection final : public ports::k8s::ExecSession,
                             public std::enable_shared_from_this<ExecConnection> {
public:
    ExecConnection(ports::k8s::ExecSession::Output output,
                   ports::k8s::ExecSession::Finished finished)
        : output_(std::move(output))
        , finished_(std::move(finished))
    {
    }

    void write(std::string_view bytes) override
    {
        send(domain::k8s::ExecChannel::stdin, bytes);
    }

    void resize(std::uint16_t columns, std::uint16_t rows) override
    {
        send(domain::k8s::ExecChannel::resize, domain::k8s::resizePayload(columns, rows));
    }

    void cancel() override { closeOnce(); }

    /// Take the socket and flush what was typed while it was opening.
    void attach(std::shared_ptr<ports::http::Socket> socket)
    {
        socket_ = std::move(socket);
        const auto weak = weak_from_this();

        socket_->onMessage([weak](const domain::http::WsFrame &frame) {
            const auto self = weak.lock();
            if (!self) {
                return;
            }
            const auto decoded = domain::k8s::decodeExecFrame(frame.payload);
            if (!decoded) {
                return;
            }
            switch (decoded->channel) {
            case domain::k8s::ExecChannel::close:
                self->closeOnce();
                return;
            case domain::k8s::ExecChannel::error:
                self->fail(domain::k8s::execFailure(decoded->payload));
                return;
            default:
                self->output_(decoded->channel, decoded->payload);
                return;
            }
        });

        socket_->onClose([weak](domain::http::HttpError error) {
            if (const auto self = weak.lock()) {
                self->fail(ApiError{.code = 0,
                                    .message = std::string{domain::http::describe(error)}});
            }
        });

        for (const auto &[channel, payload] : queued_) {
            socket_->sendBinary(domain::k8s::encodeExecFrame(channel, payload));
        }
        queued_.clear();
    }

    /// The upgrade never happened.
    void refuse(ApiError error) { fail(std::move(error)); }

private:
    void send(domain::k8s::ExecChannel channel, std::string_view payload)
    {
        if (done_) {
            return;
        }
        if (!socket_) {
            queued_.emplace_back(channel, std::string{payload});
            return;
        }
        if (socket_->isOpen()) {
            socket_->sendBinary(domain::k8s::encodeExecFrame(channel, payload));
        }
    }

    /// One exit path. The socket closes first, so a late frame cannot arrive
    /// after `onFinished` has run.
    void fail(ApiError error)
    {
        if (done_) {
            return;
        }
        done_ = true;
        closeOnce();
        finished_(std::move(error));
    }

    void closeOnce()
    {
        if (closed_) {
            return;
        }
        closed_ = true;
        if (socket_) {
            socket_->close({});
        }
    }

    std::shared_ptr<ports::http::Socket> socket_;
    std::vector<std::pair<domain::k8s::ExecChannel, std::string>> queued_;
    ports::k8s::ExecSession::Output output_;
    ports::k8s::ExecSession::Finished finished_;
    bool closed_ = false;
    bool done_ = false;
};

/// The `container`, `command`, `stdin`, `stdout`, `stderr` and `tty` query the
/// exec subresource expects.
[[nodiscard]] std::string execQuery(const ports::k8s::ExecOptions &options)
{
    std::string query = "stdin=true&stdout=true&stderr=true";
    if (options.tty) {
        query += "&tty=true";
    }
    if (!options.container.empty()) {
        query += "&container=";
        query += domain::k8s::percentEncode(options.container);
    }
    for (const std::string &argument : options.command) {
        query += "&command=";
        query += domain::k8s::percentEncode(argument);
    }
    return query;
}

} // namespace

std::expected<std::shared_ptr<ports::k8s::ExecSession>, ApiError> HttpKubeClient::exec(
    const domain::k8s::ApiPath &path, const ports::k8s::ExecOptions &options, ports::k8s::ExecSession::Output onOutput,
    ports::k8s::ExecSession::Finished onFinished)
{
    if (options.command.empty()) {
        return std::unexpected(ApiError{.code = 400, .message = "exec needs a command"});
    }

    const std::string target =
        binding_.base + path.with(domain::k8s::Subresource::exec).render() + '?' + execQuery(options);
    auto parsed = domain::http::Url::parse(target);
    if (!parsed) {
        return std::unexpected(transportFailure(parsed.error()));
    }

    ++stats_.submitted;
    auto session =
        std::make_shared<ExecConnection>(std::move(onOutput), std::move(onFinished));
    const auto weak = weak_from_this();

    http_->openSocket(
        std::move(*parsed), std::string{domain::k8s::execSubprotocol},
        [weak, session](std::shared_ptr<ports::http::Socket> opened) {
            if (const auto self = weak.lock()) {
                ++self->stats_.succeeded;
            }
            session->attach(std::move(opened));
        },
        [weak, session](domain::http::HttpError error) {
            if (const auto self = weak.lock()) {
                ++self->stats_.failed;
            }
            session->refuse(transportFailure(error));
        });

    return std::shared_ptr<ports::k8s::ExecSession>{std::move(session)};
}

} // namespace antares::adapters::k8s
