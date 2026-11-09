#pragma once

#include <cstdint>

namespace antares::domain::http {

/// Ceilings that bound every allocation a hostile peer could drive (FR-010).
struct Limits {
    std::uint8_t maxRedirects = 10;
    std::uint16_t maxStreamsPerConnection = 100;
    std::uint32_t maxHeaderBytes = 32 * 1024;
    std::uint64_t maxBodyBytes = 64ULL * 1024 * 1024;
    std::uint32_t idleTimeoutMs = 15'000;
    std::uint32_t connectTimeoutMs = 10'000;
};

} // namespace antares::domain::http
