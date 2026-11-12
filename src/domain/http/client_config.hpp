#pragma once

#include <string>

#include "domain/http/auth.hpp"
#include "ports/log/logger.hpp"
#include "domain/http/limits.hpp"
#include "domain/http/pool_config.hpp"
#include "domain/http/retry_policy.hpp"
#include "domain/http/timeouts.hpp"

namespace antares::domain::http {

/// Everything a caller may configure, in one place. The client takes this by
/// value at construction and never reads the environment itself.
struct ClientConfig {
    Limits limits;
    PoolConfig pool;
    Timeouts timeouts;
    RetryPolicy retry;
    Auth auth;

    /// Optional sink; null disables logging. Non-owning.
    ports::log::Logger *logger = nullptr;

    /// PEM anchors that replace the system trust store when set.
    std::string trustBundlePem;

    /// Hop policy: whether to follow and whether to re-send the credential.
    struct Redirects {
        bool follow = true;
        bool forwardAuth = false;
    } redirects;

    /// Verify the peer certificate and hostname (FR-009).
    bool verifyPeer = true;
    /// Ask for a coded transfer and undo it before the caller sees the body. A
    /// caller that names its own `accept-encoding` keeps it either way.
    bool acceptEncoding = true;
    /// Keep a cookie jar and honour it. Off means neither side is processed.
    bool cookies = true;

    /// User agent, so the server side can tell builds apart.
    std::string userAgent = "antares/0.1";
};

} // namespace antares::domain::http
