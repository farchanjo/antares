// Conformance for the Kubernetes API client. One case per functional
// requirement: the shape of a request, the mapping of a failure, and the
// framing of a stream, proven without a cluster.

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "domain/json/value.hpp"
#include "domain/k8s/api_path.hpp"
#include "domain/k8s/discovery.hpp"
#include "domain/k8s/exec_channel.hpp"
#include "domain/k8s/kubeconfig.hpp"
#include "domain/k8s/options.hpp"
#include "domain/k8s/status.hpp"
#include "domain/k8s/watch_event.hpp"

namespace {

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
    check(takeFrames(buffer).empty(), "a partial frame yields nothing");

    buffer += R"({"resourceVersion":"7"}}})";
    buffer += '\n';
    buffer += R"({"type":"MODIFIED","object":{"metadata":{"resourceVersion":"8"}}})";
    buffer += '\n';
    buffer += R"({"type":"ADDED")";

    const std::vector<std::string> frames = takeFrames(buffer);
    check(frames.size() == 2, "two complete frames found");
    checkEqual(buffer, R"({"type":"ADDED")", "the incomplete tail is kept");

    auto first = decodeWatchEvent(frames.front());
    check(first && *first && (**first).type == EventType::added, "first frame decodes");
    check(first && *first && (**first).produces(), "an ADDED produces an object");
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
