#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <string_view>

#include "adapters/ui/page_error.hpp"
#include "domain/k8s/scope.hpp"

namespace antares::adapters::ui {

/// The token that reaches the page after this one. Empty means the server had
/// none, and `canFetchMore` turns false (FR-002).
using PageToken = std::string;

using PageOutcome = std::expected<PageToken, PageError>;

/// Where the next page comes from.
///
/// The source owns the two halves the model must not: it calls the API and it
/// writes the page into the store, which publishes the batch as a `ChangeSet`.
/// That ordering is the contract — the change set is queued *before* the
/// completion runs, so the rows exist by the time the token is settled and a
/// scroll cannot ask for the same page twice. The completion may run on any
/// thread; the model hops to its own.
class PageSource {
public:
    using Done = std::function<void(PageOutcome)>;

    PageSource() = default;
    PageSource(const PageSource &) = delete;
    PageSource &operator=(const PageSource &) = delete;
    PageSource(PageSource &&) = delete;
    PageSource &operator=(PageSource &&) = delete;
    virtual ~PageSource() = default;

    /// Ask for the page after `continueToken`, at most `limit` objects.
    virtual void fetch(const domain::k8s::Scope &scope, std::string_view continueToken,
                       std::uint32_t limit, Done done) = 0;
};

} // namespace antares::adapters::ui
