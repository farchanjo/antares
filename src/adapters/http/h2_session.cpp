#include "adapters/http/h2_session.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "adapters/http/connection.hpp"

#include <expected>

namespace antares::adapters::http {
namespace {

constexpr std::size_t MAX_FRAME = std::size_t{16} * 1024;

std::string_view withoutPseudo(const std::string &name)
{
    return name.starts_with(':') ? std::string_view{name}.substr(1) : std::string_view{name};
}

} // namespace

H2Session::H2Session(std::shared_ptr<Connection> connection, domain::Bytes headerCeiling,
                     domain::Bytes bodyCeiling)
    : connection_(std::move(connection))
    , headerCeiling_(headerCeiling)
    , bodyCeiling_(bodyCeiling)
{
    nghttp2_session_callbacks *callbacks = nullptr;
    if (nghttp2_session_callbacks_new(&callbacks) != 0) {
        return;
    }
    configureCallbacks(callbacks);
    (void)nghttp2_session_client_new(&session_, callbacks, this);
    nghttp2_session_callbacks_del(callbacks);

    if (session_ != nullptr) {
        // Cap the peer's HPACK dynamic table so a hostile encoder cannot grow it
        // without bound (RFC 7541 §6.5).
        const nghttp2_settings_entry settings[] = {
            {NGHTTP2_SETTINGS_HEADER_TABLE_SIZE,
             static_cast<std::uint32_t>(headerCeiling_.count() / 4)},
            {NGHTTP2_SETTINGS_ENABLE_PUSH, 0},
        };
        (void)nghttp2_submit_settings(session_, NGHTTP2_FLAG_NONE, settings,
                                      sizeof settings / sizeof settings[0]);
    }
}

H2Session::~H2Session()
{
    if (session_ != nullptr) {
        nghttp2_session_del(session_);
        session_ = nullptr;
    }
}

void H2Session::configureCallbacks(nghttp2_session_callbacks *callbacks)
{
    nghttp2_session_callbacks_set_send_callback(
        callbacks, [](nghttp2_session *, const std::uint8_t *data, std::size_t length,
                      int, void *userData) -> ssize_t {
            auto *self = static_cast<H2Session *>(userData);
            self->outgoing_.append(reinterpret_cast<const char *>(data), length);
            return static_cast<ssize_t>(length);
        });

    nghttp2_session_callbacks_set_on_header_callback(
        callbacks, [](nghttp2_session *, const nghttp2_frame *frame, const std::uint8_t *name,
                      std::size_t nameLength, const std::uint8_t *value, std::size_t valueLength,
                      std::uint8_t, void *userData) -> int {
            auto *self = static_cast<H2Session *>(userData);
            if (frame->hd.type != NGHTTP2_HEADERS) {
                return 0;
            }
            auto *stream = self->find(frame->hd.stream_id);
            if (stream == nullptr) {
                return 0;
            }

            const std::string header{reinterpret_cast<const char *>(name), nameLength};
            const std::string_view field = withoutPseudo(header);

            if (field == "status") {
                unsigned int status = 0;
                const auto *first = value;
                const auto *last = value + valueLength;
                for (; first != last && *first >= '0' && *first <= '9'; ++first) {
                    status = (status * 10) + static_cast<unsigned int>(*first - '0');
                }
                stream->response.status = static_cast<std::uint16_t>(status);
                stream->response.version = domain::http::Version::http2;
                return 0;
            }
            if (header.starts_with(':')) {
                return 0;
            }

            stream->response.headers.push_back(domain::http::Header{
                header, std::string{reinterpret_cast<const char *>(value), valueLength}});
            return 0;
        });

    nghttp2_session_callbacks_set_on_frame_recv_callback(
        callbacks, [](nghttp2_session *, const nghttp2_frame *frame, void *userData) -> int {
            auto *self = static_cast<H2Session *>(userData);
            if (frame->hd.type == NGHTTP2_GOAWAY) {
                self->onGoaway(static_cast<std::int32_t>(frame->goaway.last_stream_id));
                return 0;
            }
            if (frame->hd.type == NGHTTP2_HEADERS &&
                frame->headers.cat == NGHTTP2_HCAT_RESPONSE) {
                auto *stream = self->find(frame->hd.stream_id);
                if (stream != nullptr && stream->handler.onHead) {
                    const domain::http::Response snapshot = stream->response;
                    stream->handler.onHead(snapshot);
                }
            }
            return 0;
        });

    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
        callbacks, [](nghttp2_session *, std::uint8_t, std::int32_t streamId,
                      const std::uint8_t *data, std::size_t length, void *userData) -> int {
            auto *self = static_cast<H2Session *>(userData);
            auto *stream = self->find(streamId);
            if (stream == nullptr) {
                return 0;
            }
            const auto *bytes = reinterpret_cast<const std::byte *>(data);
            if (stream->streaming) {
                if (stream->handler.onChunk) {
                    stream->handler.onChunk(std::span<const std::byte>{bytes, length});
                }
                return 0;
            }
            if (stream->response.body.size() + length > self->bodyCeiling_.count()) {
                stream->response.body.insert(stream->response.body.end(), bytes, bytes + length);
                return 0;
            }
            stream->response.body.insert(stream->response.body.end(), bytes, bytes + length);
            return 0;
        });

    nghttp2_session_callbacks_set_on_stream_close_callback(
        callbacks, [](nghttp2_session *, std::int32_t streamId, std::uint32_t errorCode,
                      void *userData) -> int {
            auto *self = static_cast<H2Session *>(userData);
            auto *stream = self->find(streamId);
            if (stream == nullptr || stream->finished) {
                return 0;
            }
            if (errorCode != NGHTTP2_NO_ERROR) {
                self->complete(streamId, std::unexpected(domain::http::HttpError::protocol));
                return 0;
            }
            self->complete(streamId, std::move(stream->response));
            return 0;
        });
}

bool H2Session::canAcceptStream() const noexcept
{
    if (closed_ || session_ == nullptr) {
        return false;
    }
    // RFC 9113 §6.5.2: the peer owns the concurrency ceiling.
    const std::uint32_t peerLimit =
        nghttp2_session_get_remote_settings(session_, NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS);
    const std::size_t ceiling = peerLimit == 0 ? maxConcurrent_ : peerLimit;
    return streams_.size() < ceiling;
}

H2Session::Stream *H2Session::find(std::int32_t streamId)
{
    auto found = streams_.find(streamId);
    return found == streams_.end() ? nullptr : &found->second;
}

std::expected<std::int32_t, domain::http::HttpError>
H2Session::submit(const domain::http::Request &request, const std::string &target,
                  const std::string &authority, Done done)
{
    return submitStreaming(request, target, authority, ports::http::StreamHandler{}, std::move(done));
}

std::expected<std::int32_t, domain::http::HttpError>
H2Session::submitStreaming(const domain::http::Request &request, const std::string &target,
                           const std::string &authority, ports::http::StreamHandler handler,
                           Done done)
{
    if (session_ == nullptr || closed_) {
        return std::unexpected(domain::http::HttpError::closed);
    }

    // The lowered names must outlive the loop: nghttp2 keeps the pointers.
    std::vector<std::string> names;
    names.reserve(request.headers.size());
    std::vector<nghttp2_nv> headers;
    headers.reserve(request.headers.size() + 4);
    auto add = [&headers](std::string_view name, std::string_view value) {
        nghttp2_nv entry{};
        entry.name = reinterpret_cast<std::uint8_t *>(const_cast<char *>(name.data()));
        entry.namelen = name.size();
        entry.value = reinterpret_cast<std::uint8_t *>(const_cast<char *>(value.data()));
        entry.valuelen = value.size();
        entry.flags = NGHTTP2_NV_FLAG_NONE;
        headers.push_back(entry);
    };

    const std::string method{domain::http::methodName(request.method)};
    const std::string scheme = connection_->url().isTls() ? "https" : "http";
    // Bound, not a temporary: the ternary would otherwise build a string that
    // dies before nghttp2 copies from it.
    const std::string path = target.empty() ? std::string{"/"} : target;
    add(":method", method);
    add(":scheme", scheme);
    // RFC 9113 §8.3.1: :authority must not be empty.
    if (authority.empty()) {
        return std::unexpected(domain::http::HttpError::protocol);
    }
    add(":authority", authority);
    add(":path", path);
    const std::string urgency = "u=" + std::to_string(request.priority);
    add("priority", urgency);
    // RFC 9113 §8.2.2: connection-specific fields are not repeated on the
    // wire; TE survives only with the value "trailers".
    for (const domain::http::Header &header : request.headers) {
        names.push_back(header.name);
        std::string &name = names.back();
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        if (name == "connection" || name == "keep-alive" || name == "proxy-connection" ||
            name == "transfer-encoding" || name == "upgrade") {
            continue;
        }
        if (name == "te" && header.value != "trailers") {
            continue;
        }
        add(name, header.value);
    }

    auto stream = std::make_unique<Stream>();
    stream->done = std::move(done);
    stream->streaming = handler.onChunk != nullptr;
    stream->handler = std::move(handler);
    stream->body.assign(reinterpret_cast<const char *>(request.body.data()), request.body.size());

    nghttp2_data_provider provider{};
    nghttp2_data_provider *providerPointer = nullptr;
    if (!stream->body.empty()) {
        provider.source.ptr = stream.get();
        provider.read_callback = [](nghttp2_session *, std::int32_t, std::uint8_t *buffer,
                                    std::size_t length, std::uint32_t *flags, nghttp2_data_source *source,
                                    void *) -> ssize_t {
            auto *target = static_cast<Stream *>(source->ptr);
            const std::size_t remaining = target->body.size() - target->bodyOffset;
            const std::size_t take = std::min(remaining, length);
            std::memcpy(buffer, target->body.data() + target->bodyOffset, take);
            target->bodyOffset += take;
            if (target->bodyOffset == target->body.size()) {
                *flags |= NGHTTP2_DATA_FLAG_EOF;
            }
            return static_cast<ssize_t>(take);
        };
        providerPointer = &provider;
    }

    const std::int32_t streamId =
        nghttp2_submit_request(session_, nullptr, headers.data(), headers.size(), providerPointer,
                               nullptr);
    if (streamId < 0) {
        return std::unexpected(domain::http::HttpError::protocol);
    }

    lastStreamId_ = streamId;
    streams_.emplace(streamId, std::move(*stream));
    // nghttp2 buffers frames; sending is what puts them in the write buffer.
    (void)nghttp2_session_send(session_);
    flush();
    return streamId;
}

void H2Session::flush()
{
    if (session_ == nullptr || outgoing_.empty()) {
        return;
    }

    std::string frame;
    frame.swap(outgoing_);
    std::fprintf(stderr, "  h2 flush %zu\n", frame.size());
    connection_->write(
        ports::io::Buffer{reinterpret_cast<const std::byte *>(frame.data()),
                          reinterpret_cast<const std::byte *>(frame.data()) + frame.size()},
        [](WriteOutcome) {});
}

void H2Session::onGoaway(std::int32_t lastStreamId)
{
    // RFC 9113 §6.8: no new stream above the announced id, and the ones above
    // it were never processed, so they are safe to retry elsewhere.
    closed_ = true;

    std::vector<std::int32_t> unprocessed;
    for (const auto &entry : streams_) {
        if (entry.first > lastStreamId) {
            unprocessed.push_back(entry.first);
        }
    }
    for (const std::int32_t id : unprocessed) {
        complete(id, std::unexpected(domain::http::HttpError::refused));
    }
}

void H2Session::receive(const std::byte *data, std::size_t length)
{
    std::fprintf(stderr, "  h2 recv %zu closed=%d\n", length, static_cast<int>(closed_));
    if (session_ == nullptr || closed_) {
        return;
    }

    const auto *bytes = reinterpret_cast<const std::uint8_t *>(data);
    if (nghttp2_session_mem_recv(session_, bytes, length) < 0) {
        fail(domain::http::HttpError::protocol);
        return;
    }
    (void)nghttp2_session_send(session_);
    flush();
}

void H2Session::complete(std::int32_t streamId,
                         std::expected<domain::http::Response, domain::http::HttpError> result)
{
    auto found = streams_.find(streamId);
    if (found == streams_.end() || found->second.finished) {
        return;
    }

    found->second.finished = true;
    auto done = std::move(found->second.done);
    streams_.erase(found);

    if (done) {
        done(std::move(result));
    }
}

void H2Session::reset(std::int32_t streamId)
{
    if (session_ == nullptr || streamId <= 0) {
        return;
    }
    complete(streamId, std::unexpected(domain::http::HttpError::cancelled));
    (void)nghttp2_submit_rst_stream(session_, NGHTTP2_FLAG_NONE, streamId, NGHTTP2_CANCEL);
    (void)nghttp2_session_send(session_);
    flush();
}

void H2Session::fail(domain::http::HttpError error)
{
    closed_ = true;
    std::vector<std::int32_t> ids;
    ids.reserve(streams_.size());
    for (const auto &entry : streams_) {
        ids.push_back(entry.first);
    }
    for (const std::int32_t id : ids) {
        complete(id, std::unexpected(error));
    }
}

} // namespace antares::adapters::http
