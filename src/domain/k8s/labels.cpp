#include "domain/k8s/labels.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

namespace antares::domain::k8s {

bool Labels::append(std::string_view key, std::string_view value)
{
    if (count_ >= maxEntries || used_ + key.size() + value.size() > maxBytes) {
        truncated_ = true;
        return false;
    }

    if (!blob_.empty()) {
        blob_.push_back('\n');
    }
    blob_.append(key);
    blob_.push_back('=');
    blob_.append(value);
    used_ = static_cast<std::uint16_t>(used_ + key.size() + value.size());
    ++count_;
    return true;
}

std::optional<Label> Labels::at(std::size_t index) const noexcept
{
    if (index >= count_) {
        return std::nullopt;
    }

    std::size_t record = 0;
    std::size_t start = 0;
    while (start < blob_.size()) {
        const std::size_t end = blob_.find('\n', start);
        const std::size_t stop = end == std::string_view::npos ? blob_.size() : end;
        if (record == index) {
            const std::string_view line{blob_.data() + start, stop - start};
            const std::size_t equals = line.find('=');
            if (equals == std::string_view::npos) {
                return std::nullopt;
            }
            return Label{line.substr(0, equals), line.substr(equals + 1)};
        }
        ++record;
        start = stop + 1;
    }
    return std::nullopt;
}

std::string_view Labels::valueOf(std::string_view key) const noexcept
{
    for (std::size_t index = 0; index < count_; ++index) {
        const std::optional<Label> label = at(index);
        if (label && label->key == key) {
            return label->value;
        }
    }
    return {};
}

} // namespace antares::domain::k8s
