#pragma once

#include <cstdint>
#include <string>

#include "domain/k8s/identity.hpp"
#include "domain/k8s/labels.hpp"
#include "domain/k8s/status_summary.hpp"

namespace antares::domain::k8s {

/// One stored object, in the only shape the store keeps.
///
/// This is a projection, not a document: the fields a list and a detail pane
/// read, and nothing else. `managedFields`, `annotations` and a `Secret`'s
/// `data`/`stringData` are decoded into the document and never copied here
/// (FR-002) — an annotation can carry a `Secret`'s payload and any of them can
/// carry a token.
///
/// The shape moves additively (D-16): a field that is added stays, and a view
/// that needs a dropped field asks the server with `get` instead.
struct Entry {
    Identity identity;
    std::string resourceVersion;
    std::uint64_t generation = 0;
    std::string creationTimestamp;
    Labels labels;
    StatusSummary status;
    /// The owning namespace. Empty for a cluster-scoped object, and also empty
    /// when the list was narrowed to one namespace and the server omits it.
    std::string namespaceName;
    /// The kind of the controller that owns the object, from the first owner
    /// reference. Empty when nothing owns it.
    std::string ownerKind;
    /// The node a pod is scheduled on.
    std::string nodeName;
    /// The pod's QoS class.
    std::string qosClass;

    [[nodiscard]] const Uid &uid() const noexcept { return identity.uid; }

    /// The CUE field of the same name: the label set hit its ceiling, so a
    /// selector over a cut key may report a false "no match".
    [[nodiscard]] bool labelsTruncated() const noexcept { return labels.truncated(); }
};

} // namespace antares::domain::k8s
