#pragma once

#include <string>
#include <vector>

#include "domain/json/value.hpp"

namespace antares::domain::k8s {

/// One resource a cluster serves, as the discovery document describes it.
///
/// `namespaced` and `verbs` are what a UI needs to decide whether a resource can
/// be listed per namespace and which actions to offer, so both are kept rather
/// than inferred.
struct ResourceDescriptor {
    std::string name;
    std::string kind;
    std::string group;
    std::string version;
    bool namespaced = true;
    std::vector<std::string> verbs;
    std::string shortNames;
};

/// One served group version, with its resources when they were asked for.
struct GroupVersionDescriptor {
    std::string group;
    std::string version;
    std::vector<ResourceDescriptor> resources;
};

/// Read a `/api` document. The core group has no name and exactly one version.
void appendCoreVersions(const json::Value &document, std::vector<GroupVersionDescriptor> &out);

/// Read an `/apis` document. Every other group arrives here, each with the
/// versions it serves.
void appendGroups(const json::Value &document, std::vector<GroupVersionDescriptor> &out);

/// Read an `APIResourceList`. Subresources arrive as `pods/log` and are kept:
/// the caller decides what to hide.
[[nodiscard]] GroupVersionDescriptor parseResourceList(const json::Value &document);

} // namespace antares::domain::k8s
