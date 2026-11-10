#include "adapters/k8s/http_kube_client.hpp"

#include <utility>

#include "domain/json/value.hpp"

namespace antares::adapters::k8s {

namespace {

using domain::k8s::ApiError;
using domain::k8s::Reason;

constexpr std::string_view jsonContentType = "application/json";
constexpr std::string_view acceptType = "application/json";

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
    built.headers.push_back(domain::http::Header{"accept", std::string{acceptType}});

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
            for (const domain::json::Value &item : document->member("items").items()) {
                ports::k8s::Object object;
                object.meta = domain::k8s::ObjectMeta::fromJson(item);
                object.body = item;
                page.items.push_back(std::move(object));
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
        domain::http::Header{"content-type", std::string{jsonContentType}});
    built.body = toBody(body.dump());
    return send(std::move(built), std::move(done));
}

std::expected<std::shared_ptr<ports::k8s::Ticket>, ApiError>
HttpKubeClient::update(const domain::k8s::ApiPath &path, domain::json::Value body,
                       std::function<void(ports::k8s::ObjectResult)> done)
{
    auto built = request(domain::http::Method::put, path.render(), {});
    built.headers.push_back(
        domain::http::Header{"content-type", std::string{jsonContentType}});
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
        domain::http::Header{"content-type", std::string{jsonContentType}});
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
    // carried between chunks (FR-004).
    auto buffer = std::make_shared<std::string>();
    auto sharedHandler = std::make_shared<ports::k8s::WatchHandler>(std::move(handler));
    const auto weak = weak_from_this();

    ports::http::StreamHandler streaming;
    streaming.onChunk = [buffer, sharedHandler, weak](std::span<const std::byte> chunk) {
        buffer->append(reinterpret_cast<const char *>(chunk.data()), chunk.size());
        for (std::string &frame : domain::k8s::takeFrames(*buffer)) {
            auto event = domain::k8s::decodeWatchEvent(frame);
            if (!event) {
                continue;
            }
            if (!*event) {
                continue;
            }
            const auto self = weak.lock();
            if (self) {
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
        for (std::string &line : domain::k8s::takeFrames(*buffer)) {
            (*sink)(line);
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
        request(domain::http::Method::get, path.render(), options.render()), std::move(streaming),
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

ports::k8s::ClientStats HttpKubeClient::stats() const noexcept
{
    return stats_;
}

} // namespace antares::adapters::k8s
