#pragma once

#include <cstdint>

#include "domain/units/bytes.hpp"

namespace antares::domain::http {

using Bytes = antares::domain::Bytes;

/// Ceilings that bound every allocation a hostile peer could drive (FR-010).
struct Limits {
    std::uint8_t maxRedirects = 10;
    std::uint16_t maxStreamsPerConnection = 100;
    Bytes maxHeaderBytes{32 * 1024};
    Bytes maxBodyBytes{64ULL * 1024 * 1024};
};

} // namespace antares::domain::http
