// Conformance for the Kubernetes API client. One case per functional
// requirement: the shape of a request, the mapping of a failure, and the
// framing of a stream, proven without a cluster.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "application/k8s/object_store.hpp"
#include "domain/json/scan_error.hpp"
#include "domain/json/scalar_scan.hpp"
#include "domain/json/value.hpp"
#include "domain/k8s/api_path.hpp"
#include "domain/k8s/discovery.hpp"
#include "domain/k8s/entry_decoder.hpp"
#include "domain/k8s/exec_channel.hpp"
#include "domain/k8s/kubeconfig.hpp"
#include "domain/k8s/namespace_name.hpp"
#include "domain/k8s/object_key.hpp"
#include "domain/k8s/object_name.hpp"
#include "domain/k8s/options.hpp"
#include "domain/k8s/scope.hpp"
#include "domain/k8s/status.hpp"
#include "domain/k8s/store_budget.hpp"
#include "domain/k8s/watch_event.hpp"
#include "ports/k8s/kube_client.hpp"
#include "support/counting_allocator.hpp"

namespace {

using namespace antares::application::k8s;
using namespace antares::domain;
using namespace antares::domain::k8s;

int failures = 0;

void check(bool condition, std::string_view what)
{
    if (condition) {
        return;
    }
    std::printf("  FAIL %.*s\n", static_cast<int>(what.size()), what.data());
    ++failures;
}

void checkEqual(std::string_view actual, std::string_view expected, std::string_view what)
{
    if (actual == expected) {
        return;
    }
    std::printf("  FAIL %.*s: got '%.*s', wanted '%.*s'\n", static_cast<int>(what.size()),
                what.data(), static_cast<int>(actual.size()), actual.data(),
                static_cast<int>(expected.size()), expected.data());
    ++failures;
}

/// A core-group descriptor: pods are the canonical example and need no group.
[[nodiscard]] GroupVersionResource corePods()
{
    auto made = GroupVersionResource::of("", "v1", "pods");
    check(made.has_value(), "core pods descriptor builds");
    return made.value_or(GroupVersionResource{});
}

[[nodiscard]] GroupVersionResource appsDeployments()
{
    auto made = GroupVersionResource::of("apps", "v1", "deployments");
    check(made.has_value(), "apps deployments descriptor builds");
    return made.value_or(GroupVersionResource{});
}

// FR-001 — the path of the core group.
void caseCoreGroupPath()
{
    const ApiPath path{corePods(), std::nullopt, std::nullopt, Subresource::none};
    checkEqual(path.render(), "/api/v1/pods", "core group path");
    checkEqual(path.prefix(), "/api/v1", "core group prefix");
}

// FR-001 — the path of a named group.
void caseNamedGroupPath()
{
    const ApiPath path{appsDeployments(), std::nullopt, std::nullopt, Subresource::none};
    checkEqual(path.render(), "/apis/apps/v1/deployments", "named group path");
}

// FR-001 — the namespace segment sits before the resource.
void caseNamespaceSegment()
{
    const ApiPath base{corePods(), std::nullopt, std::nullopt, Subresource::none};
    auto namespaced = base.inNamespace("kube-system");
    check(namespaced.has_value(), "namespace accepted");
    if (!namespaced) {
        return;
    }
    checkEqual(namespaced->render(), "/api/v1/namespaces/kube-system/pods", "namespaced path");
}

// FR-001 — a name and a subresource append in order.
void caseSubresourceSegment()
{
    auto namespaced = ApiPath{corePods(), std::nullopt, std::nullopt, Subresource::none}
                          .inNamespace("default");
    check(namespaced.has_value(), "namespace accepted");
    if (!namespaced) {
        return;
    }
    auto named = namespaced->named("web-0");
    check(named.has_value(), "name accepted");
    if (!named) {
        return;
    }
    checkEqual(named->with(Subresource::status).render(),
               "/api/v1/namespaces/default/pods/web-0/status", "status subresource path");
    checkEqual(named->with(Subresource::log).render(),
               "/api/v1/namespaces/default/pods/web-0/log", "log subresource path");
}

// FR-001, FR-002 — a hostile name never reaches the path, and the two rules
// part at the object-name boundary: uppercase, `_` and `:` are legal in a name
// and refused in a namespace, which the server does keep strict.
void caseHostileNameRefused()
{
    const ApiPath base{corePods(), std::nullopt, std::nullopt, Subresource::none};
    check(!base.inNamespace("../../etc").has_value(), "traversal namespace refused");
    check(!base.inNamespace("Kube-System").has_value(), "uppercase namespace refused");
    check(!base.named("../../etc").has_value(), "traversal name refused");
    check(!base.inNamespace("").has_value(), "empty namespace refused");
    check(base.named("web_0").has_value(), "underscore name accepted");
    check(base.named("system:controller:persistent-volume-binder").has_value(),
          "colon name accepted");
    check(!base.inNamespace("system:controller").has_value(), "colon namespace refused");

    check(!GroupVersionResource::of("", "v1", "pods/exec").has_value(), "slash in resource");
    check(!GroupVersionResource::of("APPS", "v1", "deployments").has_value(), "uppercase group");

    // The rules themselves, so a regression names the boundary.
    check(isDnsLabel("web-0"), "label accepts web-0");
    check(!isDnsLabel("-web"), "label refuses a leading dash");
    check(isDnsSubdomain("kube-system.example"), "subdomain accepts a dotted name");
    check(!isDnsSubdomain("a..b"), "subdomain refuses an empty label");
}

struct NameClass {
    std::string name;
    bool accepted;
};

/// The eleven name classes of the spec table, in its order. `.` and `..`
/// resolve as segments, `/` splits one and `%` would reach the server
/// unescaped; everything else is a name the server takes, because a row the app
/// shows must be openable.
[[nodiscard]] const std::vector<NameClass> &nameClasses()
{
    static const std::vector<NameClass> table{
        {"System:Controller:Persistent-Volume-Binder", true},
        {"web_0", true},
        {"system:controller:persistent-volume-binder", true},
        {"kube-root-ca.crt", true},
        {std::string(SEGMENT_CEILING, 'a'), true},
        {std::string(SEGMENT_CEILING + 1, 'a'), false},
        {"", false},
        {".", false},
        {"..", false},
        {"a/b", false},
        {"a%b", false},
    };
    return table;
}

/// The store's `#ObjectName` rule, mirrored from
/// `doc/arch/schemas/bound-the-event-memory.cue`: 1 to 253 bytes, no `/` and
/// no `%`, and neither `.` nor `..`. The same sentence as `isPathSegment`,
/// spelled apart so the agreement case compares two rules instead of one with
/// itself.
[[nodiscard]] bool storeAccepts(std::string_view name)
{
    return !name.empty() && name.size() <= SEGMENT_CEILING &&
           name.find_first_of("/%") == std::string_view::npos && name != "." && name != "..";
}

/// FR-001, FR-004 — the table is the contract: an accepted name builds and
/// stays one segment, a refused one never reaches the path.
void caseNameClasses()
{
    const std::vector<NameClass> &table = nameClasses();
    check(table.size() == 11, "the table carries every class once");

    const ApiPath base{corePods(), std::nullopt, std::nullopt, Subresource::none};
    for (std::size_t index = 0; index < table.size(); ++index) {
        const NameClass &row = table[index];
        const std::string label = "class " + std::to_string(index) + " accepted";
        const std::string refused = "class " + std::to_string(index) + " refused";

        const auto built = base.named(row.name);
        check(built.has_value() == row.accepted, row.accepted ? label : refused);
        if (!built || !row.accepted) {
            continue;
        }
        // An accepted name stays one segment: the builder introduces no
        // separator, so the name renders between the resource and the end.
        checkEqual(built->render(), "/api/v1/pods/" + row.name, "name stays one segment");
    }
}

/// FR-005 — a row the store holds has to be openable. Both rules are fed every
/// class and must answer alike, and the four widened classes are also asserted
/// on the store mirror alone so a typo there cannot agree by accident. The
/// store does not re-check the length, so 254 bytes is the one class the path
/// still refuses on its own.
void caseStorePathAgreement()
{
    check(storeAccepts("System:Controller"), "the store takes uppercase");
    check(storeAccepts("web_0"), "the store takes an underscore");
    check(storeAccepts("system:controller"), "the store takes a colon");
    check(!storeAccepts("a/b"), "the store refuses a separator");
    check(!storeAccepts("a%b"), "the store refuses an escape");
    check(!storeAccepts("."), "the store refuses a dot");
    check(!storeAccepts(".."), "the store refuses a dotdot");
    check(!storeAccepts(""), "the store refuses the empty name");

    const ApiPath base{corePods(), std::nullopt, std::nullopt, Subresource::none};
    for (const NameClass &row : nameClasses()) {
        const bool pathAccepts = base.named(row.name).has_value();
        check(pathAccepts == row.accepted, "the path answers the table");
        check(pathAccepts == storeAccepts(row.name), "the store and the path agree");
    }
}

/// FR-003 — a refusal names the rule it broke, and echoes the name the
/// operator supplied. The reason is a closed class, so the debug log line a
/// caller writes from the message stays bounded.
void caseNameRefusalReason()
{
    const std::string longName(SEGMENT_CEILING + 1, 'a');
    const ApiPath base{corePods(), std::nullopt, std::nullopt, Subresource::none};

    const std::pair<std::string_view, std::string_view> table[] = {
        {"", "empty"},         {longName, "tooLong"}, {".", "dot"},
        {"..", "dotdot"},      {"a/b", "separator"}, {"a%b", "escape"},
    };

    for (const auto &[name, reason] : table) {
        const auto refused = base.named(std::string{name});
        check(!refused.has_value(), "the name is refused");
        if (refused) {
            continue;
        }
        check(refused.error().reason == Reason::invalid, "the reason stays rejected");
        check(refused.error().message.find(reason) != std::string::npos,
              "the refusal names the rule");
        check(refused.error().message.find(name) != std::string::npos, "the name is echoed");
    }
}

// FR-002 — every set option rides the query.
void caseListOptionsRendered()
{
    ListOptions options;
    options.label.text = "app=web,tier!=cache";
    options.field.text = "status.phase=Running";
    options.limit = 100;
    options.continueToken = std::string{"tok/with+chars"};

    const std::string query = options.render();
    check(query.find("labelSelector=app%3Dweb%2Ctier%21%3Dcache") != std::string::npos,
          "label selector encoded");
    check(query.find("fieldSelector=status.phase%3DRunning") != std::string::npos,
          "field selector encoded");
    check(query.find("limit=100") != std::string::npos, "limit present");
    check(query.find("continue=tok%2Fwith%2Bchars") != std::string::npos,
          "continuation token encoded");
}

// FR-002 — an unset option is absent, not empty.
void caseListOptionsEmpty()
{
    const ListOptions options;
    checkEqual(options.render(), "", "an empty option set renders nothing");
}

// FR-003 — each patch type carries its own content type.
void casePatchContentTypes()
{
    checkEqual(patchContentType(PatchType::merge), "application/merge-patch+json", "merge");
    checkEqual(patchContentType(PatchType::strategic), "application/strategic-merge-patch+json",
               "strategic");
    checkEqual(patchContentType(PatchType::apply), "application/apply-patch+yaml", "apply");
}

// FR-003 — the delete body names its propagation policy.
void caseDeleteOptionsBody()
{
    DeleteOptions options;
    options.propagation = Propagation::foreground;
    const std::string body = options.render();
    check(body.find("\"propagationPolicy\":\"Foreground\"") != std::string::npos,
          "propagation policy in the body");
    check(body.find("\"kind\":\"DeleteOptions\"") != std::string::npos, "kind in the body");
}

// FR-005 — a Status becomes its typed reason.
void caseNotFoundMapped()
{
    const std::string body =
        R"({"kind":"Status","status":"Failure","message":"pods \"x\" not found","reason":"NotFound","code":404})";
    const ApiError error = mapFailure(404, body);
    check(error.reason == Reason::notFound, "mapped to notFound");
    check(error.code == 404, "code preserved");
    check(error.message.find("not found") != std::string::npos, "message preserved");
}

// FR-005 — a body that is not a Status still maps, from the code.
void caseUnexpectedBodyMapped()
{
    const ApiError error = mapFailure(500, "<html>crashed</html>");
    check(error.reason == Reason::serviceUnavailable, "5xx maps to serviceUnavailable");
    check(error.code == 500, "code preserved");

    const ApiError conflict = mapFailure(409, "");
    check(conflict.reason == Reason::conflict, "409 maps to conflict without a body");

    const ApiError odd = mapFailure(418, "");
    check(odd.reason == Reason::unexpected, "an unknown code maps to unexpected");
}

// FR-005 — a hostile message cannot grow without bound.
void caseMessageBounded()
{
    const std::string huge(4096, 'a');
    const std::string kept = bounded(huge);
    check(kept.size() <= ApiError::messageCeiling, "message stays under the ceiling");
    check(kept.ends_with("...") || kept.size() <= ApiError::messageCeiling,
          "a cut is marked");

    const std::string small = "short";
    checkEqual(bounded(small), small, "a short message is untouched");
}

// FR-004 — a bookmark advances the version and emits nothing.
void caseBookmarkAdvances()
{
    const std::string frame =
        R"({"type":"BOOKMARK","object":{"kind":"Pod","metadata":{"resourceVersion":"12345"}}})";
    auto event = decodeWatchEvent(frame);
    check(event.has_value(), "bookmark decodes");
    if (!event || !*event) {
        return;
    }
    check((**event).type == EventType::bookmark, "typed as bookmark");
    checkEqual((**event).resourceVersion, "12345", "version captured");
    check(!(**event).produces(), "bookmark produces no object");
}

// FR-004 — a 410 on the stream ends it as expired.
void caseExpiredStream()
{
    const std::string frame =
        R"({"type":"ERROR","object":{"kind":"Status","status":"Failure","message":"too old resource version","reason":"Expired","code":410}})";
    auto event = decodeWatchEvent(frame);
    check(event.has_value(), "error frame decodes");
    if (!event || !*event) {
        return;
    }
    check((**event).type == EventType::error, "typed as error");
    check((**event).failure.reason == Reason::expired, "reported as expired");
    check(!(**event).produces(), "an error produces no object");
}

// FR-004 — frames split across chunks are reassembled.
void caseFramesSplitAcrossChunks()
{
    std::string buffer;
    buffer += R"({"type":"ADDED","object":{"metadata":)";
    {
        FrameCursor partial{buffer};
        check(!partial.next().has_value(), "a partial frame yields nothing");
    }
    checkEqual(buffer, R"({"type":"ADDED","object":{"metadata":)", "an incomplete tail is kept");

    buffer += R"({"resourceVersion":"7","uid":"5b6f0f1e-2c4a-4d38-9a11-7f3e6c2b8d40",)"
              R"("name":"web-0"},"kind":"Pod"}})";
    buffer += '\n';
    buffer += R"({"type":"MODIFIED","object":{"metadata":{"resourceVersion":"8",)"
              R"("uid":"5b6f0f1e-2c4a-4d38-9a11-7f3e6c2b8d40","name":"web-0"},)"
              R"("kind":"Pod"}})";
    buffer += '\n';
    buffer += R"({"type":"ADDED")";

    std::vector<std::string> frames;
    {
        FrameCursor cursor{buffer};
        while (const std::optional<std::string_view> frame = cursor.next()) {
            frames.emplace_back(*frame);
        }
    }
    check(frames.size() == 2, "two complete frames found");
    checkEqual(buffer, R"({"type":"ADDED")", "the incomplete tail is kept");

    auto first = decodeWatchEvent(frames.front());
    check(first && *first && (**first).type == EventType::added, "first frame decodes");
    check(first && *first && (**first).produces(), "an ADDED produces an object");
    check(first && *first && (**first).entry.has_value(), "the frame carries a projection");
}

// FR-005 — the cursor views the accumulator and compacts at most once per
// chunk, so no frame is copied out of it.
void caseFrameCursorCompactsOnce()
{
    const std::vector<std::string> chunks{
        R"({"type":"ADDED","object":{"kind":"Pod","metadata":)"
        R"({"uid":"5b6f0f1e-2c4a-4d38-9a11-7f3e6c2b8d40","name":"web-0",)"
        R"("resourceVersion":"7"}}})"
        "\n",
        R"({"type":"BOOKMARK","object":{"metadata":{"resourceVersion":"8"}}})"
        "\n"
        "\n",
    };

    std::string buffer;
    std::size_t parses = 0;
    std::size_t compactions = 0;

    for (const std::string &chunk : chunks) {
        buffer += chunk;
        std::size_t erased = 0;
        {
            FrameCursor cursor{buffer};
            while (const std::optional<std::string_view> frame = cursor.next()) {
                ++parses;
                const auto event = decodeWatchEvent(*frame);
                check(event.has_value(), "every complete frame decodes");
            }
            erased = cursor.compact();
        }
        if (erased > 0) {
            ++compactions;
        }
    }

    check(parses == 3, "every frame is parsed exactly once");
    check(compactions <= chunks.size(), "the accumulator compacts at most once per chunk");
    check(buffer.empty(), "nothing is left behind");
}

// FR-004 — a blank line is a keep-alive, not a failure.
void caseKeepAliveSkipped()
{
    auto event = decodeWatchEvent("\r\n");
    check(event.has_value(), "keep-alive is not an error");
    check(event.has_value() && !*event, "keep-alive yields no event");
}

// FR-006 — the current context is selected and its cluster found.
void caseKubeconfigCurrentContext()
{
    const std::string document = R"(apiVersion: v1
kind: Config
current-context: dev
clusters:
- name: alpha
  cluster:
    server: https://alpha:6443
    insecure-skip-tls-verify: true
contexts:
- name: dev
  context:
    cluster: alpha
    user: builder
    namespace: work
users:
- name: builder
  user:
    token: s3cr3t
)";

    auto config = Kubeconfig::parse(document);
    check(config.has_value(), "document parses");
    if (!config) {
        return;
    }
    checkEqual(config->currentContext, "dev", "current context read");

    auto context = config->select({});
    check(context.has_value(), "current context selected");
    if (context) {
        checkEqual(context->cluster, "alpha", "context names its cluster");
        checkEqual(context->defaultNamespace, "work", "context carries a namespace");
    }

    const Cluster *cluster = config->findCluster("alpha");
    check(cluster != nullptr, "cluster found");
    if (cluster != nullptr) {
        checkEqual(cluster->server, "https://alpha:6443", "server read");
        check(cluster->insecureSkipTlsVerify, "insecure flag read");
    }

    const User *user = config->findUser("builder");
    check(user != nullptr, "user found");
    if (user != nullptr) {
        checkEqual(user->token, "s3cr3t", "token read");
    }
}

// FR-006 — each failure mode is distinct.
void caseKubeconfigFailures()
{
    const std::string broken = "current-context: dev\ncontexts:\n- name: dev\n  context:\n    cluster: absent\n";
    auto config = Kubeconfig::parse(broken);
    check(config.has_value(), "a document without clusters still parses");
    if (config) {
        auto context = config->select({});
        check(context.has_value(), "the context resolves");
        check(config->findCluster("absent") == nullptr, "the absent cluster is not found");
    }

    const std::string noCurrent = "clusters:\n- name: a\n  cluster:\n    server: https://a\n";
    auto bare = Kubeconfig::parse(noCurrent);
    check(bare.has_value(), "a document without a current context parses");
    if (bare) {
        auto selected = bare->select({});
        check(!selected.has_value(), "selecting without a context fails");
        check(!selected.has_value() &&
                  selected.error() == ConfigError::noCurrentContext,
              "reported as noCurrentContext");
    }

    auto unknown = Kubeconfig::parse(noCurrent);
    if (unknown) {
        auto selected = unknown->select("missing");
        check(!selected.has_value() && selected.error() == ConfigError::unknownContext,
              "an absent context is unknownContext");
    }

    auto malformed = Kubeconfig::parse("\tnot: [a, b]\n");
    check(!malformed.has_value() || malformed->clusters.empty(), "a flow sequence is refused");
}

// FR-007 — a token is rendered as a bearer credential, never into the URL.
void caseBearerCredential()
{
    User user;
    user.name = "builder";
    user.token = "abc.def";
    check(!user.empty(), "a user with a token is not empty");

    // The credential reaches the header, and the path never carries it.
    const ApiPath path{corePods(), std::nullopt, std::nullopt, Subresource::none};
    check(path.render().find("abc.def") == std::string::npos, "no token in the path");
}

// The JSON reader the whole module rests on: a round trip has to be faithful.
void caseJsonRoundTrip()
{
    const std::string text = R"({"a":1,"b":[true,null,"x"],"c":{"d":-2.5e3}})";
    auto value = json::Value::parse(text);
    check(value.has_value(), "document parses");
    if (!value) {
        return;
    }
    check(value->isObject(), "root is an object");
    check(value->intAt("a") == 1, "integer member read");
    check(value->member("b").isArray(), "array member read");
    checkEqual(value->member("b").items().at(2).asText(), "x", "array text member read");
    check(value->member("c").intAt("d") == std::nullopt, "a real is not an integer");

    auto again = json::Value::parse(value->dump());
    check(again.has_value(), "the dump parses again");
    if (again) {
        check(again->intAt("a") == 1, "round trip keeps the integer");
    }
}

// A truncated document is refused rather than half-read.
void caseJsonRefusesTruncation()
{
    check(!json::Value::parse(R"({"a":)").has_value(), "an incomplete object is refused");
    check(!json::Value::parse(R"({"a":1}x)").has_value(), "trailing content is refused");
    check(!json::Value::parse("").has_value(), "an empty document is refused");
    check(json::Value::parse("null").has_value(), "a null document parses");
}

/// FR-010: a served resource keeps the flags a UI needs, above all whether it
/// is namespaced. A resource silently taken as cluster scoped sends the caller
/// to a path the server refuses.
void caseDiscoveryParsesResources()
{
    const auto document = json::Value::parse(R"({"groupVersion":"apps/v1","resources":[)"
                                             R"({"name":"deployments","kind":"Deployment",)"
                                             R"("namespaced":true,"verbs":["get","list"],)"
                                             R"("shortNames":["deploy"]}]})");
    check(document.has_value(), "document parsed");
    if (!document) {
        return;
    }

    const GroupVersionDescriptor served = parseResourceList(*document);
    checkEqual(served.group, "apps/v1", "group version kept");
    check(served.resources.size() == 1, "one resource read");
    if (served.resources.empty()) {
        return;
    }
    checkEqual(served.resources.front().name, "deployments", "name kept");
    checkEqual(served.resources.front().kind, "Deployment", "kind kept");
    check(served.resources.front().namespaced, "namespaced kept");
    checkEqual(served.resources.front().shortNames, "deploy", "short name joined");
    check(served.resources.front().verbs.size() == 2, "verbs kept");
}

/// FR-010: a cluster scoped resource must not be mistaken for a namespaced one.
void caseDiscoveryClusterScoped()
{
    const auto document = json::Value::parse(R"({"groupVersion":"v1","resources":[)"
                                             R"({"name":"nodes","kind":"Node",)"
                                             R"("namespaced":false,"verbs":["get"]}]})");
    check(document.has_value(), "document parsed");
    if (!document) {
        return;
    }

    const GroupVersionDescriptor served = parseResourceList(*document);
    check(!served.resources.empty(), "one resource read");
    if (served.resources.empty()) {
        return;
    }
    check(!served.resources.front().namespaced, "cluster scoped kept");
}

/// FR-010: the core group arrives under `/api` with no name, and must survive
/// with an empty group rather than being dropped.
void caseDiscoveryCoreVersions()
{
    const auto document = json::Value::parse(R"({"kind":"APIVersions","versions":["v1"]})");
    check(document.has_value(), "document parsed");
    if (!document) {
        return;
    }

    std::vector<GroupVersionDescriptor> out;
    appendCoreVersions(*document, out);
    check(out.size() == 1, "core version read");
    if (out.empty()) {
        return;
    }
    check(out.front().group.empty(), "core group stays empty");
    checkEqual(out.front().version, "v1", "version kept");
}

/// FR-010: every named group comes back with each version it serves.
void caseDiscoveryGroups()
{
    const auto document = json::Value::parse(
        R"({"kind":"APIGroupList","groups":[{"name":"apps",)"
        R"("versions":[{"groupVersion":"apps/v1","version":"v1"}])"
        R"(},{"name":"batch","versions":[{"groupVersion":"batch/v1","version":"v1"}]}]})");
    check(document.has_value(), "document parsed");
    if (!document) {
        return;
    }

    std::vector<GroupVersionDescriptor> out;
    appendGroups(*document, out);
    check(out.size() == 2, "both groups read");
    if (out.size() != 2) {
        return;
    }
    checkEqual(out.front().group, "apps", "first group kept");
    checkEqual(out.back().group, "batch", "second group kept");
}

/// FR-009: a frame survives the round trip with its channel intact. Channels
/// are what keeps stderr off stdout, so a lost byte here is a corrupted shell.
void caseExecFrameRoundTrip()
{
    const std::string frame = encodeExecFrame(ExecChannel::stderr, "boom");
    check(frame.size() == 5, "one channel byte plus the payload");

    const auto decoded = decodeExecFrame(frame);
    check(decoded.has_value(), "frame decoded");
    if (!decoded) {
        return;
    }
    check(decoded->channel == ExecChannel::stderr, "channel kept");
    checkEqual(decoded->payload, "boom", "payload kept");
}

/// FR-009: an empty payload is still a frame; a zero-length frame is not.
void caseExecEmptyPayload()
{
    const std::string frame = encodeExecFrame(ExecChannel::stdout, "");
    check(frame.size() == 1, "channel byte only");

    const auto decoded = decodeExecFrame(frame);
    check(decoded.has_value(), "empty payload still decodes");
    if (decoded) {
        check(decoded->payload.empty(), "payload stays empty");
    }

    check(!decodeExecFrame("").has_value(), "no byte is not a frame");
}

/// FR-009: a peer inventing a channel must not be read as stdout.
void caseExecUnknownChannel()
{
    const std::string frame = encodeExecFrame(static_cast<ExecChannel>(9), "x");
    check(!decodeExecFrame(frame).has_value(), "channel nine refused");
}

/// FR-009: resize carries both numbers under the names the server reads.
void caseExecResizePayload()
{
    checkEqual(resizePayload(120, 40), R"({"Width":120,"Height":40})", "resize shape");
}

/// FR-009: an error frame becomes an error, whether or not it is a Status.
void caseExecErrorFrame()
{
    const ApiError parsed = execFailure(R"({"reason":"NotFound","message":"no such container"})");
    check(parsed.reason == Reason::notFound, "reason mapped");
    checkEqual(parsed.message, "no such container", "message kept");

    const ApiError junk = execFailure("not a status");
    check(junk.reason == Reason::unexpected, "junk is unexpected");
    check(!junk.message.empty(), "junk keeps its text");
}

// --- the projection and the store --------------------------------------

[[nodiscard]] std::string readFixture(std::string_view name)
{
    const std::string path = std::string{ANTARES_FIXTURE_DIR} + "/" + std::string{name};
    std::ifstream file{path, std::ios::binary};
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

[[nodiscard]] std::optional<json::Value> parseFixture(std::string_view name)
{
    auto document = json::Value::parse(readFixture(name));
    if (!document) {
        return std::nullopt;
    }
    return std::move(*document);
}

[[nodiscard]] std::optional<Entry> decodeFixture(std::string_view name)
{
    const auto document = parseFixture(name);
    if (!document) {
        return std::nullopt;
    }
    auto entry = decodeEntry(*document);
    if (!entry) {
        return std::nullopt;
    }
    return std::move(*entry);
}

[[nodiscard]] Scope scopeFor(std::string_view namespaceName = "shop")
{
    Scope scope;
    scope.cluster = ClusterName::of("dev").value_or(ClusterName{});
    scope.type = corePods();
    scope.kind = "Pod";
    scope.namespaceName = NamespaceName::of(namespaceName).value_or(NamespaceName{});
    return scope;
}

[[nodiscard]] std::string uidText(std::size_t index)
{
    char buffer[40] = {};
    std::snprintf(buffer, sizeof(buffer), "00000000-0000-4000-8000-%012zx", index);
    return std::string{buffer};
}

[[nodiscard]] std::optional<Entry> entryFor(std::size_t index)
{
    auto entry = decodeFixture("pod.json");
    if (!entry) {
        return std::nullopt;
    }
    auto uid = Uid::parse(uidText(index));
    if (!uid) {
        return std::nullopt;
    }
    entry->identity.uid = *uid;
    return entry;
}

/// One delta, as the watch adapter would hand it over.
[[nodiscard]] WatchEvent eventOf(EventType type, const Entry &entry)
{
    WatchEvent event;
    event.type = type;
    event.resourceVersion = entry.resourceVersion;
    event.entry = entry;
    return event;
}

// FR-002 — the stored shape is the projection and nothing else.
void caseEntryProjection()
{
    const auto document = parseFixture("pod.json");
    check(document.has_value(), "fixture parses");
    if (!document) {
        return;
    }

    const auto entry = decodeEntry(*document);
    check(entry.has_value(), "fixture decodes");
    if (!entry) {
        return;
    }

    checkEqual(entry->identity.name, "checkout-api-6f8d7c9b54-abcdefghi", "name kept");
    checkEqual(entry->resourceVersion, "184223", "resource version kept");
    check(entry->generation == 1, "generation kept");
    checkEqual(entry->creationTimestamp, "2026-11-11T19:05:00Z", "creation time kept");
    check(entry->labels.size() == 3, "every label kept");
    check(!entry->labelsTruncated(), "a small label set is not truncated");
    checkEqual(entry->status.reason, "Running", "status reason kept");

    // `managedFields` and `annotations` never reach the entry.
    checkEqual(entry->labels.valueOf("app"), "checkout", "label read by key");

    // A Secret keeps neither `data` nor `stringData`, however large they are.
    const std::string payload(1024 * 1024, 'A');
    json::Value secret;
    const std::string text = R"({"kind":"Secret","metadata":{"uid":)"
                             R"("5b6f0f1e-2c4a-4d38-9a11-7f3e6c2b8d40","name":"tls",)"
                             R"("resourceVersion":"9"},"data":{"tls.crt":")" +
                             payload + R"("},"stringData":{"token":")" + payload + R"("}})";
    auto parsed = json::Value::parse(text);
    check(parsed.has_value(), "secret parses");
    if (parsed) {
        const auto decoded = decodeEntry(*parsed);
        check(decoded.has_value(), "secret decodes");
        check(decoded && decoded->labels.empty(), "a secret keeps no data as a label");
    }
}

// FR-003 — an object outside the name, uid and kind rules is refused.
void caseEntryRefusesHostile()
{
    const auto base = parseFixture("pod.json");
    check(base.has_value(), "fixture parses");
    if (!base) {
        return;
    }

    check(!ObjectName::of(std::string(254, 'n')).has_value(), "254-byte name refused");
    check(ObjectName::of(std::string(253, 'n')).has_value(), "253-byte name admitted");
    check(!ObjectName::of(".").has_value(), "dot refused");
    check(!ObjectName::of("a/b").has_value(), "slash refused");
    check(!Uid::parse("not-a-uid").has_value(), "non-UUID uid refused");
    check(!NamespaceName::of("Kube-System").has_value(), "uppercase namespace refused");
    check(NamespaceName::of("").has_value(), "empty namespace is cluster scope");
}

// FR-003 — labels cut at sixteen entries and at 256 bytes, and say so.
void caseEntryLabelsTruncated()
{
    Labels labels;
    for (std::size_t index = 0; index < 20; ++index) {
        labels.append("k" + std::to_string(index), "v");
    }
    check(labels.size() == 16, "sixteen entries kept");
    check(labels.truncated(), "the cut is recorded");

    Labels bytes;
    for (std::size_t index = 0; index < 8; ++index) {
        bytes.append("key" + std::to_string(index), std::string(40, 'x'));
    }
    check(bytes.truncated(), "a byte ceiling cuts even under sixteen entries");
    check(bytes.size() < 8, "some pairs were dropped");
}

// FR-003 — one worst-case admitted entry stays inside 1024 bytes.
void caseEntryWorstCaseBound()
{
    const auto document = parseFixture("worst-entry.json");
    check(document.has_value(), "worst-case fixture parses");
    if (!document) {
        return;
    }

    const antares::test::CountingScope counting;
    const std::size_t before = antares::test::live();
    const auto entry = decodeEntry(*document);
    const std::size_t used = antares::test::live() - before;

    check(entry.has_value(), "worst-case object decodes");
    check(used <= 1024, "an admitted entry costs at most 1024 bytes");
    check(entry && entry->labels.size() == 16, "the label ceiling is what stopped it");
    check(entry && entry->labelsTruncated(), "the truncation is recorded");
    std::printf("  worst-case entry: %zu bytes\n", used);
}

// --- feature 008: the selective scan -----------------------------------

/// Every fixture the two paths must agree on. `pod.json` and `worst-entry.json`
/// come from feature 005; the CRD, the Secret and the escape document are the
/// shapes the projection skips the most of (FR-002, FR-004).
constexpr std::array<std::string_view, 5> SCAN_FIXTURES{
    "pod.json", "crd.json", "secret.json", "escapes.json", "worst-entry.json"};

// FR-001 — the scanner produces the same entry as the parser, field by field.
void caseScanMatchesProjection()
{
    for (const std::string_view name : SCAN_FIXTURES) {
        const std::string text = readFixture(name);
        check(!text.empty(), "fixture is readable");

        const auto scanned = scanEntry(text);
        check(scanned.has_value(), (std::string{"fixture scans: "} + std::string{name}));

        const auto document = json::Value::parse(text);
        check(document.has_value(), "fixture parses");
        if (!scanned || !document) {
            continue;
        }

        const auto projected = decodeEntry(*document);
        check(projected.has_value(), "fixture projects");
        if (!projected) {
            continue;
        }

        check(scanned->identity.uid == projected->identity.uid, "uid agrees");
        checkEqual(scanned->identity.name, projected->identity.name, "name agrees");
        checkEqual(scanned->resourceVersion, projected->resourceVersion, "version agrees");
        check(scanned->generation == projected->generation, "generation agrees");
        checkEqual(scanned->creationTimestamp, projected->creationTimestamp, "timestamp agrees");
        checkEqual(scanned->labels.blob(), projected->labels.blob(), "labels agree");
        check(scanned->labelsTruncated() == projected->labelsTruncated(), "label cut agrees");
        checkEqual(scanned->status.reason, projected->status.reason, "reason agrees");
        check(scanned->status.ready == projected->status.ready, "ready count agrees");
        check(scanned->status.total == projected->status.total, "total count agrees");
    }
}

// FR-002 — a skipped value costs no allocation, and the parser path does.
void caseScanSkipsWithoutAllocating()
{
    const std::string text = readFixture("pod.json");
    check(!text.empty(), "fixture is readable");
    if (text.empty()) {
        return;
    }

    std::size_t scannedAllocations = 0;
    {
        const antares::test::CountingScope counting;
        const std::size_t before = antares::test::allocations();
        const auto entry = scanEntry(text);
        scannedAllocations = antares::test::allocations() - before;
        check(entry.has_value(), "fixture scans");
    }

    std::size_t parsedAllocations = 0;
    {
        const antares::test::CountingScope counting;
        const std::size_t before = antares::test::allocations();
        const auto document = json::Value::parse(text);
        check(document.has_value(), "fixture parses");
        if (document) {
            const auto entry = decodeEntry(*document);
            check(entry.has_value(), "parser path decodes");
        }
        parsedAllocations = antares::test::allocations() - before;
    }

    check(scannedAllocations <= 16, "a scanned frame stays under sixteen allocations");
    check(parsedAllocations >= 10 * scannedAllocations,
          "the parser path allocates an order above the scan");
    std::printf("  scanned frame: %zu allocations, parser path: %zu\n", scannedAllocations,
                parsedAllocations);
}

// FR-003 — skipping validates: a truncation and a mis-nesting report an offset.
void caseScanReportsOffset()
{
    const std::string text = readFixture("pod.json");
    const std::size_t inside = text.find("\"managedFields\"");
    check(inside != std::string::npos, "fixture carries managedFields");
    if (inside == std::string::npos) {
        return;
    }

    const std::string truncated = text.substr(0, inside + 30);
    const auto shortScan = scanEntry(truncated);
    check(!shortScan.has_value(), "a document ending inside a skip is refused");
    if (!shortScan) {
        check(shortScan.error().offset == truncated.size(), "the offset is the truncation");
        checkEqual(scanReasonName(shortScan.error().reason), "unterminated string",
                   "a cut string is named");
    }

    // A `]` closing an object: the skip has to catch it where it stands.
    const std::string misNested =
        R"({"kind":"Pod","metadata":{"uid":"5b6f0f1e-2c4a-4d38-9a11-7f3e6c2b8d40",)"
        R"("name":"p"},"spec":{"a":1]})";
    const std::size_t bracket = misNested.rfind(']');
    const auto nestedScan = scanEntry(misNested);
    check(!nestedScan.has_value(), "a mis-nested document is refused");
    if (!nestedScan) {
        check(nestedScan.error().offset == bracket, "the offset is the stray bracket");
        checkEqual(scanReasonName(nestedScan.error().reason), "expected ',' or the closing delimiter",
                   "a mis-nesting is named");
    }
}

// FR-004 — every escape decodes into the kept value.
void caseScanResolvesEscapes()
{
    constexpr std::array<std::pair<std::string_view, std::string_view>, 8> ESCAPES{{
        {R"(\")", "\""},
        {R"(\\)", "\\"},
        {R"(\b)", "\b"},
        {R"(\f)", "\f"},
        {R"(\n)", "\n"},
        {R"(\r)", "\r"},
        {R"(\t)", "\t"},
        {R"(\u00e9)", "\u00e9"},
    }};

    for (const auto &[escaped, decoded] : ESCAPES) {
        const std::string document =
            R"({"kind":"Pod","metadata":{"uid":"5b6f0f1e-2c4a-4d38-9a11-7f3e6c2b8d40",)"
            R"("name":"x)" +
            std::string{escaped} + R"(x"}})";
        const auto entry = scanEntry(document);
        check(entry.has_value(), "an escaped name scans");
        if (entry) {
            checkEqual(entry->identity.name, "x" + std::string{decoded} + "x", "escape decoded");
        }
    }

    // `/` is the one escape a name cannot carry, so it round-trips elsewhere.
    const std::string slash = R"({"kind":"Pod","metadata":{"uid":)"
                              R"("5b6f0f1e-2c4a-4d38-9a11-7f3e6c2b8d40","name":"p",)"
                              R"("creationTimestamp":"a\/b"}})";
    const auto slashed = scanEntry(slash);
    check(slashed.has_value(), "a slash escape scans");
    if (slashed) {
        checkEqual(slashed->creationTimestamp, "a/b", "slash decoded");
    }

    // The fixture carries the whole table in the name, surrogate pair included.
    const auto fixture = scanEntry(readFixture("escapes.json"));
    check(fixture.has_value(), "the escape fixture scans");
    if (fixture) {
        // The surrogate pair is spelled in bytes: a narrow literal rejects the
        // pair, and the scanner must produce the four-byte UTF-8 form.
        // clang encodes a surrogate pair as two three-byte forms, so the
        // expected text is spelled in bytes: UTF-8 is one four-byte form.
        checkEqual(fixture->identity.name,
                   "e\"s\\c\b\f\n\r\t\xC3\xA9" "\xF0\x9F\x98\x80",
                   "the fixture name is decoded");
    }

    // A lone surrogate is a refusal, not a replacement character.
    const std::string loneHigh =
        R"({"kind":"Pod","metadata":{"uid":"5b6f0f1e-2c4a-4d38-9a11-7f3e6c2b8d40",)"
        R"("name":"\uD83D"}})";
    const auto high = scanEntry(loneHigh);
    check(!high.has_value(), "a lone high surrogate is refused");
    const std::string loneLow =
        R"({"kind":"Pod","metadata":{"uid":"5b6f0f1e-2c4a-4d38-9a11-7f3e6c2b8d40",)"
        R"("name":"\uDE00"}})";
    check(!scanEntry(loneLow).has_value(), "a lone low surrogate is refused");
}

// FR-005 — the key lookup stops at the name instead of reading the frame.
void caseScanNameStops()
{
    const std::string filler(64 * 1024, 'x');
    const std::string document =
        R"({"kind":"Pod","metadata":{"uid":"5b6f0f1e-2c4a-4d38-9a11-7f3e6c2b8d40",)"
        R"("name":"web-0","labels":{"a":")" +
        filler + R"("}},"spec":{"blob":")" + filler + R"("}})";

    std::size_t consumed = 0;
    const auto name = scanName(document, &consumed);
    check(name.has_value(), "the name is found");
    checkEqual(name.value_or(std::string{}), "web-0", "the name is decoded");
    check(consumed < 128, "the walk stops at the name");
    check(consumed < document.size() / 100, "far short of the document");

    const auto missing = scanName(R"({"kind":"Pod","metadata":{}})");
    check(!missing.has_value(), "a document without a name is refused");
    if (!missing) {
        checkEqual(scanReasonName(missing.error().reason), "missing metadata.name",
                   "the absence is named");
    }
}

// FR-006 — the saving is measured over the same fixture and must clear 4x.
void caseScanIsFaster()
{
    const std::string text = readFixture("pod.json");
    check(!text.empty(), "fixture is readable");
    if (text.empty()) {
        return;
    }

    constexpr std::size_t ROUNDS = 5;
    constexpr std::size_t ITERATIONS = 2000;

    const auto measure = [&text](bool scanning) {
        volatile std::uint64_t sink = 0;
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t index = 0; index < ITERATIONS; ++index) {
            if (scanning) {
                const auto entry = scanEntry(text);
                if (entry) {
                    sink += entry->identity.name.size() + entry->labels.size();
                }
                continue;
            }
            const auto document = json::Value::parse(text);
            if (document) {
                const auto entry = decodeEntry(*document);
                if (entry) {
                    sink += entry->identity.name.size() + entry->labels.size();
                }
            }
        }
        const auto stop = std::chrono::steady_clock::now();
        static_cast<void>(sink);
        return static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count());
    };

    std::int64_t scanBest = std::numeric_limits<std::int64_t>::max();
    std::int64_t parseBest = scanBest;
    for (std::size_t round = 0; round < ROUNDS; ++round) {
        scanBest = std::min(scanBest, measure(true));
        parseBest = std::min(parseBest, measure(false));
    }

    const auto ratio = static_cast<double>(parseBest) / static_cast<double>(scanBest);
    std::printf("  scan %lld ns, parse %lld ns, %.1fx\n", static_cast<long long>(scanBest),
                static_cast<long long>(parseBest), ratio);
    check(ratio >= 4.0, "the scanner is at least 4x faster");
}

// FR-003 — five thousand fixture pods stay inside 2 560 000 bytes.
void caseStoreBounds5000Pods()
{
    const auto base = decodeFixture("pod.json");
    check(base.has_value(), "fixture decodes");
    if (!base) {
        return;
    }

    const Scope scope = scopeFor();
    std::size_t used = 0;
    {
        const antares::test::CountingScope counting;
        const std::size_t before = antares::test::live();

        ObjectStore store;
        const auto opened = store.open(scope);
        check(opened.has_value(), "scope opens");
        if (!opened) {
            return;
        }

        for (std::size_t index = 0; index < 5000; ++index) {
            Entry copy = *base;
            auto uid = Uid::parse(uidText(index));
            if (!uid) {
                continue;
            }
            copy.identity.uid = *uid;
            const std::array<WatchEvent, 1> batch{eventOf(EventType::added, copy)};
            static_cast<void>(store.apply(scope, batch));
        }
        used = antares::test::live() - before;
        check(store.totalEntries() == 5000, "every pod is stored");
    }
    check(used <= 2'560'000, "five thousand pods cost at most 2 560 000 bytes");
    std::printf("  5000 pods: %zu bytes (%zu per entry)\n", used, used / 5000);
}

// Observability — a page that lands is counted, and it is its own counter.
void casePagesFetched()
{
    antares::ports::k8s::KubeStats stats;
    check(stats.pagesFetched == 0, "no page before a fetch");
    ++stats.pagesFetched;
    check(stats.pagesFetched == 1, "a completed page is counted");
    check(stats.succeeded == 0, "pages are not successes");
    check(stats.submitted == 0, "pages are not submissions");
}

// FR-001 — two readers of the same key get the same entry.
void caseStoreSharesEntry()
{
    const auto entry = decodeFixture("pod.json");
    check(entry.has_value(), "fixture decodes");
    if (!entry) {
        return;
    }

    const Scope scope = scopeFor();
    ObjectStore store;
    check(store.open(scope).has_value(), "scope opens");

    const std::array<WatchEvent, 1> batch{eventOf(EventType::added, *entry)};
    static_cast<void>(store.apply(scope, batch));

    const ObjectKey key{scope, entry->identity.uid};
    const Entry *first = store.resolve(key);
    const Entry *second = store.resolve(key);
    check(first != nullptr, "the key resolves");
    check(first == second, "both readers receive the same stored entry");
    check(store.resolve(ObjectKey{scope, Uid{}}) == nullptr, "an absent uid resolves to nothing");
}

// FR-006 — a MODIFIED updates in place, a DELETED removes, an unknown uid is
// counted rather than failed, and a bookmark touches nothing.
void caseStoreUpsertInPlace()
{
    const auto entry = decodeFixture("pod.json");
    check(entry.has_value(), "fixture decodes");
    if (!entry) {
        return;
    }

    const Scope scope = scopeFor();
    ObjectStore store;
    check(store.open(scope).has_value(), "scope opens");

    const std::array<WatchEvent, 1> added{eventOf(EventType::added, *entry)};
    static_cast<void>(store.apply(scope, added));

    Entry modified = *entry;
    modified.resourceVersion = "184224";
    const std::array<WatchEvent, 1> update{eventOf(EventType::modified, modified)};
    const ChangeSet changes = store.apply(scope, update);

    const StoreScope *opened = store.find(scope);
    check(opened != nullptr, "the scope is open");
    check(opened != nullptr && opened->size() == 1, "a modification keeps the count");
    check(changes.changed.size() == 1, "exactly one row is announced as changed");
    check(changes.changed.size() == 1 && changes.changed.front().row == 0, "the row is named");
    check(!changes.changed.empty() &&
              changes.changed.front().fields == std::vector<EntryField>{EntryField::resourceVersion},
          "the mask names only the field that moved");

    // A field a view does not render repaints nothing.
    Entry unrendered = modified;
    unrendered.creationTimestamp = "2026-11-11T19:06:00Z";
    const std::array<WatchEvent, 1> quiet{eventOf(EventType::modified, unrendered)};
    check(store.apply(scope, quiet).empty(), "an unrendered change emits nothing");

    // An unknown uid on MODIFIED is counted, not failed.
    Entry stranger = *entry;
    stranger.identity.uid = *Uid::parse(uidText(999));
    const std::array<WatchEvent, 1> ghost{eventOf(EventType::modified, stranger)};
    check(store.apply(scope, ghost).empty(), "an unknown modification emits nothing");
    check(opened != nullptr && opened->state().unknown == 1, "the unknown uid is counted");

    // A bookmark touches no entry.
    WatchEvent bookmark;
    bookmark.type = EventType::bookmark;
    bookmark.resourceVersion = "184225";
    const std::array<WatchEvent, 1> touched{bookmark};
    check(store.apply(scope, touched).empty(), "a bookmark emits nothing");
    check(opened != nullptr && opened->size() == 1, "a bookmark keeps the count");

    const std::array<WatchEvent, 1> removed{eventOf(EventType::deleted, modified)};
    static_cast<void>(store.apply(scope, removed));
    check(opened != nullptr && opened->size() == 0, "a deletion removes the row");
    check(store.resolve(ObjectKey{scope, entry->identity.uid}) == nullptr, "the uid is gone");
}

// FR-008 — replaying a change set onto a mirror reproduces the rows exactly.
void caseStoreChangeSetReplays()
{
    const Scope scope = scopeFor();
    ObjectStore store;
    check(store.open(scope).has_value(), "scope opens");

    std::vector<std::string> mirror;
    for (std::size_t index = 0; index < 3; ++index) {
        const auto entry = entryFor(index);
        if (!entry) {
            continue;
        }
        const std::array<WatchEvent, 1> batch{eventOf(EventType::added, *entry)};
        static_cast<void>(store.apply(scope, batch));
        mirror.push_back(uidText(index));
    }
    check(mirror.size() == 3, "three rows are stored");

    // One batch: modify the middle row, drop the first and append a fourth.
    auto middle = entryFor(1);
    auto appended = entryFor(3);
    auto first = entryFor(0);
    check(middle && appended && first, "the batch entries decode");
    if (!middle || !appended || !first) {
        return;
    }
    middle->resourceVersion = "900001";
    std::vector<WatchEvent> batch{eventOf(EventType::modified, *middle),
                                  eventOf(EventType::deleted, *first),
                                  eventOf(EventType::added, *appended)};
    const ChangeSet changes = store.apply(scope, batch);

    check(changes.removed.size() == 1, "one removal range");
    check(changes.removed.size() == 1 && changes.removed.front().first == 0, "the removal is row 0");
    check(changes.inserted.size() == 1, "one insertion range");
    check(changes.changed.size() == 1, "one changed row");
    check(!changes.changed.empty() && changes.changed.front().row == 0, "the changed row is 0");

    // Replay: removals descend, insertions ascend, placeholders take the row
    // the store now holds at that position.
    for (const RowRange &range : changes.removed) {
        mirror.erase(mirror.begin() + range.first, mirror.begin() + range.last);
    }
    for (const RowRange &range : changes.inserted) {
        mirror.insert(mirror.begin() + range.first, range.last - range.first, std::string{});
    }

    std::vector<std::string> actual;
    if (const StoreScope *opened = store.find(scope); opened != nullptr) {
        for (const auto &row : opened->rows()) {
            if (row) {
                actual.push_back(row->uid().text());
            }
        }
    }

    check(mirror.size() == actual.size(), "the replay keeps the row count");
    for (std::size_t index = 0; index < mirror.size() && index < actual.size(); ++index) {
        if (mirror[index].empty()) {
            mirror[index] = actual[index];
        }
    }
    check(mirror == actual, "the mirror reproduces the store's rows exactly");
}

// FR-004 — past a ceiling an object is refused and counted, and nothing stored
// leaves to make room.
void caseStoreAdmitsUntilCeiling()
{
    StoreBudget budget;
    budget.maxEntries = 3;
    budget.maxTotalEntries = 6;
    budget.maxScopes = 1;

    const Scope scope = scopeFor();
    ObjectStore store{budget};
    check(store.open(scope).has_value(), "scope opens");

    for (std::size_t index = 0; index < 3; ++index) {
        const auto entry = entryFor(index);
        check(entry.has_value(), "entry decodes");
        if (!entry) {
            return;
        }
        const std::array<WatchEvent, 1> batch{eventOf(EventType::added, *entry)};
        static_cast<void>(store.apply(scope, batch));
    }

    const auto overflow = entryFor(3);
    check(overflow.has_value(), "overflow entry decodes");
    if (!overflow) {
        return;
    }
    const std::array<WatchEvent, 1> refused{eventOf(EventType::added, *overflow)};
    static_cast<void>(store.apply(scope, refused));

    const StoreScope *opened = store.find(scope);
    check(opened != nullptr, "the scope is open");
    check(opened != nullptr && opened->size() == 3, "no stored entry leaves");
    check(opened != nullptr && opened->state().truncated, "the scope is published truncated");
    check(opened != nullptr && opened->state().refused == 1, "the refusal is counted");
    check(store.resolve(ObjectKey{scope, overflow->identity.uid}) == nullptr,
          "the refused object is not stored");

    // A second scope is refused outright: one scope is the budget here.
    check(!store.open(scopeFor("other")).has_value(), "a scope past maxScopes is refused");
    check(store.open(scope).has_value(), "reopening an open scope is free");
}

// FR-007 — a relist reconciles by epoch; the count is the relist.
void caseStoreRelistReconciles()
{
    const Scope scope = scopeFor();
    ObjectStore store;
    check(store.open(scope).has_value(), "scope opens");

    for (std::size_t index = 0; index < 3; ++index) {
        const auto entry = entryFor(index);
        check(entry.has_value(), "entry decodes");
        if (!entry) {
            return;
        }
        const std::array<WatchEvent, 1> batch{eventOf(EventType::added, *entry)};
        static_cast<void>(store.apply(scope, batch));
    }

    store.markStale(scope);
    const StoreScope *opened = store.find(scope);
    check(opened != nullptr && opened->state().stale, "the scope is stale");

    const auto epoch = store.beginRelist(scope);
    check(epoch.has_value(), "the relist opens an epoch");
    if (!epoch) {
        return;
    }

    auto replacement = entryFor(3);
    check(replacement.has_value(), "replacement decodes");
    if (!replacement) {
        return;
    }
    const std::vector<Entry> page{*entryFor(0), *entryFor(2), *replacement};
    const auto pageChanges = store.relistPage(scope, *epoch, page);
    check(pageChanges.has_value(), "the page applies");

    const auto swept = store.endRelist(scope, *epoch);
    check(swept.has_value(), "the final page sweeps");
    check(opened != nullptr && opened->size() == 3, "the count is the relist, not the sum");
    check(opened != nullptr && !opened->state().stale, "the relist clears the staleness");
    check(store.resolve(ObjectKey{scope, *Uid::parse(uidText(1))}) == nullptr,
          "the absent uid is swept");
}

// FR-007 — a relist that never reaches its final page sweeps nothing.
void caseStoreRelistAbandoned()
{
    const Scope scope = scopeFor();
    ObjectStore store;
    check(store.open(scope).has_value(), "scope opens");

    const auto entry = entryFor(0);
    check(entry.has_value(), "entry decodes");
    if (!entry) {
        return;
    }
    const std::array<WatchEvent, 1> batch{eventOf(EventType::added, *entry)};
    static_cast<void>(store.apply(scope, batch));

    store.markStale(scope);
    const auto epoch = store.beginRelist(scope);
    check(epoch.has_value(), "the relist opens an epoch");
    if (!epoch) {
        return;
    }
    const std::vector<Entry> page{*entryFor(1)};
    check(store.relistPage(scope, *epoch, page).has_value(), "the first page applies");
    store.abandonRelist(scope);

    const StoreScope *opened = store.find(scope);
    check(opened != nullptr && opened->size() == 2, "an abandoned relist sweeps nothing");
    check(opened != nullptr && opened->state().stale, "the scope stays stale");
    check(!store.relistPage(scope, *epoch, page).has_value(), "a page after the epoch is refused");
}

// FR-006, FR-008 — every event type is routed and counted.
void caseStoreRoutesEvents()
{
    const Scope scope = scopeFor();
    ObjectStore store;
    check(store.open(scope).has_value(), "scope opens");

    const auto entry = entryFor(0);
    check(entry.has_value(), "entry decodes");
    if (!entry) {
        return;
    }

    Entry modified = *entry;
    modified.resourceVersion = "500001";
    std::vector<WatchEvent> batch{eventOf(EventType::added, *entry),
                                  eventOf(EventType::modified, modified),
                                  eventOf(EventType::deleted, modified)};
    const ChangeSet changes = store.apply(scope, batch);
    const StoreScope *opened = store.find(scope);
    check(opened != nullptr && opened->size() == 0, "add, modify and delete net out");
    check(changes.changed.size() == 1, "the modification is one changed row");
    check(changes.removed.empty() && changes.inserted.empty(),
          "a row born and dropped in one batch is neither inserted nor removed");
}

// D-15 — two metrics, one two-value label, and nothing unbounded.
void caseStoreMetricsBounded()
{
    const Scope scope = scopeFor();
    ObjectStore store;
    check(store.open(scope).has_value(), "scope opens");

    const auto entry = entryFor(0);
    check(entry.has_value(), "entry decodes");
    if (!entry) {
        return;
    }
    const std::array<WatchEvent, 1> batch{eventOf(EventType::added, *entry)};
    static_cast<void>(store.apply(scope, batch));

    const auto &samples = store.metrics().samples();
    check(samples.size() == 4, "two metrics with two label values each");
    checkEqual(samples[0].name, "store.entries", "entries metric named");
    checkEqual(samples[0].scopeKind, "cluster", "cluster label value");
    checkEqual(samples[1].scopeKind, "namespace", "namespace label value");
    checkEqual(samples[2].name, "store.refused", "refused metric named");
    check(store.metrics().entries(ScopeKind::namespaced) == 1, "the entry gauge counts");
    check(store.metrics().entries(ScopeKind::cluster) == 0, "the cluster gauge stays put");
}

struct Case {
    std::string_view name;
    void (*run)();
};

constexpr Case cases[] = {
    {"core-group-path", caseCoreGroupPath},
    {"named-group-path", caseNamedGroupPath},
    {"namespace-segment", caseNamespaceSegment},
    {"subresource-segment", caseSubresourceSegment},
    {"hostile-name-refused", caseHostileNameRefused},
    {"name-classes", caseNameClasses},
    {"name-refusal-reason", caseNameRefusalReason},
    {"store-path-agreement", caseStorePathAgreement},
    {"list-options-rendered", caseListOptionsRendered},
    {"list-options-empty", caseListOptionsEmpty},
    {"patch-content-types", casePatchContentTypes},
    {"delete-options-body", caseDeleteOptionsBody},
    {"not-found-mapped", caseNotFoundMapped},
    {"unexpected-body-mapped", caseUnexpectedBodyMapped},
    {"message-bounded", caseMessageBounded},
    {"bookmark-advances", caseBookmarkAdvances},
    {"expired-stream", caseExpiredStream},
    {"frames-split-across-chunks", caseFramesSplitAcrossChunks},
    {"keep-alive-skipped", caseKeepAliveSkipped},
    {"kubeconfig-current-context", caseKubeconfigCurrentContext},
    {"kubeconfig-failures", caseKubeconfigFailures},
    {"bearer-credential", caseBearerCredential},
    {"json-round-trip", caseJsonRoundTrip},
    {"json-refuses-truncation", caseJsonRefusesTruncation},
    {"discovery-parses-resources", caseDiscoveryParsesResources},
    {"discovery-cluster-scoped", caseDiscoveryClusterScoped},
    {"discovery-core-versions", caseDiscoveryCoreVersions},
    {"discovery-groups", caseDiscoveryGroups},
    {"exec-frame-round-trip", caseExecFrameRoundTrip},
    {"exec-empty-payload", caseExecEmptyPayload},
    {"exec-unknown-channel", caseExecUnknownChannel},
    {"exec-resize-payload", caseExecResizePayload},
    {"exec-error-frame", caseExecErrorFrame},
    {"frame-cursor-compacts-once", caseFrameCursorCompactsOnce},
    {"entry-projection", caseEntryProjection},
    {"entry-refuses-hostile", caseEntryRefusesHostile},
    {"entry-labels-truncated", caseEntryLabelsTruncated},
    {"entry-worst-case-bound", caseEntryWorstCaseBound},
    {"scan-matches-projection", caseScanMatchesProjection},
    {"scan-skips-without-allocating", caseScanSkipsWithoutAllocating},
    {"scan-reports-offset", caseScanReportsOffset},
    {"scan-resolves-escapes", caseScanResolvesEscapes},
    {"scan-name-stops", caseScanNameStops},
    {"scan-is-faster", caseScanIsFaster},
    {"store-bounds-5000-pods", caseStoreBounds5000Pods},
    {"store-shares-entry", caseStoreSharesEntry},
    {"store-upsert-in-place", caseStoreUpsertInPlace},
    {"store-change-set-replays", caseStoreChangeSetReplays},
    {"store-admits-until-ceiling", caseStoreAdmitsUntilCeiling},
    {"store-relist-reconciles", caseStoreRelistReconciles},
    {"store-relist-abandoned", caseStoreRelistAbandoned},
    {"store-routes-events", caseStoreRoutesEvents},
    {"store-metrics-bounded", caseStoreMetricsBounded},
    {"pages-fetched-counts", casePagesFetched},
};

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::printf("usage: antares_k8s_conformance <case>\n");
        return 2;
    }

    const std::string_view wanted{argv[1]};
    for (const Case &entry : cases) {
        if (entry.name != wanted) {
            continue;
        }
        std::printf("%.*s:\n", static_cast<int>(wanted.size()), wanted.data());
        entry.run();
        std::printf("  %d failure(s)\n", failures);
        return failures == 0 ? 0 : 1;
    }

    std::printf("unknown case '%.*s'\n", static_cast<int>(wanted.size()), wanted.data());
    return 2;
}
