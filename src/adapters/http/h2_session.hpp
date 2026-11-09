#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nghttp2/nghttp2.h>

#include "domain/http/http_error.hpp"
#include "domain/http/request.hpp"
#include "domain/http/response.hpp"
#include "domain/units/bytes.hpp"
#include "ports/http/client.hpp"

namespace antares::adapters::http {

class Connection;

/// One HTTP/2 connection, many streams (FR-019).
///
/// nghttp2 frames; this class owns the streams, the header block and the
/// completion of each one. Streams complete out of order and independently, so
/// a slow response never blocks a fast one on the same connection.
class H2Session : public std::enable_shared_from_this<H2Session> {
public:
    using Done = std::function<void(std::expected<domain::http::Response, domain::http::HttpError>)>;

    H2Session(std::shared_ptr<Connection> connection, domain::Bytes headerCeiling,
             domain::Bytes bodyCeiling);
    ~H2Session();

    H2Session(const H2Session &) = delete;
    H2Session &operator=(const H2Session &) = delete;

    /// Queue a request. Returns the stream id, or the error that stopped it.
    [[nodiscard]] std::expected<std::int32_t, domain::http::HttpError>
    submit(const domain::http::Request &request, const std::string &target,
           const std::string &authority, Done done);

    /// Same, with the body delivered per DATA frame instead of accumulated.
    [[nodiscard]] std::expected<std::int32_t, domain::http::HttpError>
    submitStreaming(const domain::http::Request &request, const std::string &target,
                    const std::string &authority, ports::http::StreamHandler handler, Done done);

    /// Push queued frames to the connection.
    void flush();

    /// Feed received bytes. `hasMore` false means the read side is done.
    void receive(const std::byte *data, std::size_t length);

    /// Complete every outstanding stream with `error`.
    void fail(domain::http::HttpError error);
    void onGoaway(std::int32_t lastStreamId);

    [[nodiscard]] std::size_t activeStreams() const noexcept { return streams_.size(); }
    [[nodiscard]] const std::shared_ptr<Connection> &connection() const noexcept
    {
        return connection_;
    }
    [[nodiscard]] bool isClosed() const noexcept { return closed_; }
    [[nodiscard]] bool canAcceptStream() const noexcept;

    static void configureCallbacks(nghttp2_session_callbacks *callbacks);

private:
    struct Stream {
        domain::http::Response response;
        Done done;
        std::string body;
        std::size_t bodyOffset = 0;
        bool finished = false;
        bool streaming = false;
        ports::http::StreamHandler handler;
    };

    void complete(std::int32_t streamId, std::expected<domain::http::Response,
                                                       domain::http::HttpError> result);
    [[nodiscard]] Stream *find(std::int32_t streamId);

    std::shared_ptr<Connection> connection_;
    nghttp2_session *session_ = nullptr;
    std::map<std::int32_t, Stream> streams_;
    std::string outgoing_;
    domain::Bytes headerCeiling_;
    domain::Bytes bodyCeiling_;
    std::int32_t lastStreamId_ = 0;
    std::size_t maxConcurrent_ = 100;
    bool closed_ = false;
};

} // namespace antares::adapters::http
