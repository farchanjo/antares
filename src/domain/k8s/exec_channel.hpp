#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "domain/k8s/api_error.hpp"

namespace antares::domain::k8s {

/// The `v5.channel.k8s.io` subprotocol (ADR-0019). A frame is one byte naming
/// its channel followed by the payload, so a TTY sees the bytes the process
/// wrote and nothing is base64'd on the way.
enum class ExecChannel : std::uint8_t {
    stdin = 0,
    stdout = 1,
    stderr = 2,
    /// A `Status` document. Only ever sent to fail the session.
    error = 3,
    /// `{"Width":c,"Height":r}`, sent by us.
    resize = 4,
    close = 5,
};

/// One decoded frame.
struct ExecFrame {
    ExecChannel channel = ExecChannel::stdout;
    std::string payload;
};

/// The subprotocol token to offer in the upgrade.
inline constexpr std::string_view execSubprotocol = "v5.channel.k8s.io";

/// Prefix `payload` with its channel.
[[nodiscard]] std::string encodeExecFrame(ExecChannel channel, std::string_view payload);

/// Split a frame. An empty payload is a valid frame; a zero-length frame is not.
[[nodiscard]] std::optional<ExecFrame> decodeExecFrame(std::string_view frame);

/// The resize payload for a terminal of `columns` by `rows`.
[[nodiscard]] std::string resizePayload(std::uint16_t columns, std::uint16_t rows);

/// Read a channel-3 payload as the failure it is. A payload that is not a
/// `Status` still produces an error, so a broken peer cannot pass silently.
[[nodiscard]] ApiError execFailure(std::string_view payload);

} // namespace antares::domain::k8s
