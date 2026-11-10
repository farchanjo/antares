#include "domain/k8s/exec_channel.hpp"

#include "domain/k8s/status.hpp"

#include <utility>

namespace antares::domain::k8s {

std::string encodeExecFrame(ExecChannel channel, std::string_view payload)
{
    std::string frame;
    frame.reserve(payload.size() + 1);
    frame.push_back(static_cast<char>(channel));
    frame.append(payload);
    return frame;
}

std::optional<ExecFrame> decodeExecFrame(std::string_view frame)
{
    if (frame.empty()) {
        return std::nullopt;
    }

    const auto channel = static_cast<std::uint8_t>(frame.front());
    // A peer that invents a channel would otherwise be read as stdout, which is
    // the one thing a TTY cannot recover from.
    if (channel > static_cast<std::uint8_t>(ExecChannel::close)) {
        return std::nullopt;
    }

    return ExecFrame{static_cast<ExecChannel>(channel), std::string{frame.substr(1)}};
}

std::string resizePayload(std::uint16_t columns, std::uint16_t rows)
{
    return "{\"Width\":" + std::to_string(columns) + ",\"Height\":" + std::to_string(rows) + '}';
}

ApiError execFailure(std::string_view payload)
{
    // The peer is meant to send a Status. When it does not, the reason is
    // genuinely unknown, so it is `unexpected` rather than a code invented from
    // the HTTP status that never arrived.
    const auto document = json::Value::parse(payload);
    if (document && document->isObject()) {
        return mapFailure(500, payload);
    }

    ApiError error;
    error.message.assign(payload.substr(0, ApiError::messageCeiling));
    return error;
}

} // namespace antares::domain::k8s
