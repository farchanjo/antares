// DDD role: ValueObject

package schemas

// #HttpVersion is what ALPN negotiated. The caller never picks it by hand.
#HttpVersion: "h2" | "http/1.1"

// #HttpMethod covers what Kubernetes needs; extension methods stay strings.
#HttpMethod: "GET" | "POST" | "PUT" | "PATCH" | "DELETE" | "HEAD" | "OPTIONS"

// #HttpError is closed: every failure the client can report.
#HttpError: "connect" | "tls" | "alpn" | "timeout" | "cancelled" | "reentrant" | "tooManyRedirects" | "headerCeiling" | "bodyCeiling" | "streamCeiling" | "protocol" | "closed"

// #HttpRequest is one submission. `version` overrides ALPN and is test-only.
#HttpRequest: {
    method:   #HttpMethod
    url:      string & =~"^https?://"
    headers?: [string]: string
    body?:    bytes
    version?: #HttpVersion
}

// #HttpResponse is what a caller receives.
#HttpResponse: {
    status:  uint16 & >=100 & <=599
    version: #HttpVersion
    headers: [string]: string
    body:    bytes
    hops:    uint8
}

