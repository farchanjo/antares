#pragma once

#include <string>

#include "domain/http/auth.hpp"
#include "domain/http/limits.hpp"
#include "domain/http/pool_config.hpp"
#include "domain/http/timeouts.hpp"

namespace antares::domain::http {

/// Everything a caller may configure, in one place. The client takes this by
/// value at construction and never reads the environment itself.
struct ClientConfig {
    Limits limits;
    PoolConfig pool;
    Timeouts timeouts;
    Auth auth;

    /// Hop policy: whether to follow and whether to re-send the credential.
    struct Redirects {
        bool follow = true;
        bool forwardAuth = false;
    } redirects;

    /// Verify the peer certificate and hostname (FR-009).
    bool verifyPeer = true;

    /// User agent, so the server side can tell builds apart.
    std::string userAgent = "antares/0.1";
};

} // namespace antares::domain::http
