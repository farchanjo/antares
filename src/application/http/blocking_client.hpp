#pragma once

#include <condition_variable>
#include <expected>
#include <memory>
#include <mutex>

#include "domain/http/client_config.hpp"
#include "domain/http/request.hpp"
#include "domain/http/response.hpp"
#include "ports/http/client.hpp"
#include "ports/io/io_engine.hpp"

namespace antares::application::http {

/// Blocking facade over the async port (FR-008).
///
/// It waits on the *request's* condition variable, never on engine state, so no
/// engine lock is held across the wait. Called on the loop thread it reports
/// `reentrant` instead of deadlocking.
class BlockingClient {
public:
    BlockingClient(ports::http::Client &client, ports::io::IoEngine &engine)
        : client_(client)
        , engine_(engine)
    {
    }

    [[nodiscard]] std::expected<domain::http::Response, domain::http::HttpError>
    send(domain::http::Request request);

private:
    ports::http::Client &client_;
    ports::io::IoEngine &engine_;
};

} // namespace antares::application::http
