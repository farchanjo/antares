#include "domain/k8s/discovery.hpp"

namespace antares::domain::k8s {
namespace {

/// Short names are a list in the document and one comma-joined string here: the
/// only consumer renders them, and a container type would not earn its keep.
[[nodiscard]] std::string joinShortNames(const json::Value &entry)
{
    std::string joined;
    for (const json::Value &alias : entry.member("shortNames").items()) {
        if (!alias.isString()) {
            continue;
        }
        if (!joined.empty()) {
            joined += ',';
        }
        joined += alias.asText();
    }
    return joined;
}

} // namespace

void appendCoreVersions(const json::Value &document, std::vector<GroupVersionDescriptor> &out)
{
    for (const json::Value &version : document.member("versions").items()) {
        if (!version.isString()) {
            continue;
        }
        out.push_back(GroupVersionDescriptor{std::string{}, std::string{version.asText()}, {}});
    }
}

void appendGroups(const json::Value &document, std::vector<GroupVersionDescriptor> &out)
{
    for (const json::Value &group : document.member("groups").items()) {
        const std::string name{group.textAt("name")};
        if (name.empty()) {
            continue;
        }
        for (const json::Value &version : group.member("versions").items()) {
            const std::string value{version.textAt("version")};
            if (value.empty()) {
                continue;
            }
            out.push_back(GroupVersionDescriptor{name, value, {}});
        }
    }
}

GroupVersionDescriptor parseResourceList(const json::Value &document)
{
    GroupVersionDescriptor served;
    served.group = std::string{document.textAt("groupVersion")};

    for (const json::Value &entry : document.member("resources").items()) {
        ResourceDescriptor resource;
        resource.name = std::string{entry.textAt("name")};
        if (resource.name.empty()) {
            continue;
        }
        resource.kind = std::string{entry.textAt("kind")};
        resource.namespaced = entry.member("namespaced").asBool();
        for (const json::Value &verb : entry.member("verbs").items()) {
            if (verb.isString()) {
                resource.verbs.emplace_back(verb.asText());
            }
        }
        resource.shortNames = joinShortNames(entry);
        served.resources.push_back(std::move(resource));
    }
    return served;
}

} // namespace antares::domain::k8s
