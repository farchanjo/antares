#include "adapters/http/h2_session.hpp"

#include <algorithm>
#include <cstring>

#include "adapters/http/connection.hpp"

namespace antares::adapters::http {
namespace {

constexpr std::size_t MAX_FRAME = 16 * 1024;

std::string_view withoutPseudo(const std::string &name)
{
    return name.rfind(':', 0) == 0 ? std::string_view{name}.substr(1) : std::string_view{name};
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
        (void)nghttp2_submit_settings(session_, NGHTTP2_FLAG_NONE, nullptr, 0);
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
            if (header.rfind(':', 0) == 0) {
                return 0;
            }

            stream->response.headers.push_back(domain::http::Header{
                header, std::string{reinterpret_cast<const char *>(value), valueLength}});
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
            if (stream->response.body.size() + length > self->bodyCeiling_.count()) {
                stream->response.body.insert(stream->response.body.end(),
                                             reinterpret_cast<const std::byte *>(data),
                                             reinterpret_cast<const std::byte *>(data) + length);
                return 0;
            }
            stream->response.body.insert(stream->response.body.end(),
                                         reinterpret_cast<const std::byte *>(data),
                                         reinterpret_cast<const std::byte *>(data) + length);
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
    return !closed_ && session_ != nullptr && streams_.size() < maxConcurrent_;
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
    if (session_ == nullptr || closed_) {
        return std::unexpected(domain::http::HttpError::closed);
    }

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
    add(":method", method);
    add(":scheme", scheme);
    add(":authority", authority);
    add(":path", target.empty() ? "/" : target);
    for (const domain::http::Header &header : request.headers) {
        add(header.name, header.value);
    }

    auto stream = std::make_unique<Stream>();
    stream->done = std::move(done);
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
    connection_->write(
        ports::io::Buffer{reinterpret_cast<const std::byte *>(frame.data()),
                          reinterpret_cast<const std::byte *>(frame.data()) + frame.size()},
        [](std::expected<std::size_t, domain::http::HttpError>) {});
}

void H2Session::receive(const std::byte *data, std::size_t length)
{
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
