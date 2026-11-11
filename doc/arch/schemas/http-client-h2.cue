// DDD role: ValueObject

package schemas

// #HttpVersion is what ALPN negotiated. The caller never picks it by hand.
#HttpVersion: "h2" | "http/1.1"

// #HttpMethod covers what Kubernetes needs; extension methods stay strings.
#HttpMethod: "GET" | "POST" | "PUT" | "PATCH" | "DELETE" | "HEAD" | "OPTIONS"

// #HttpError is closed: every failure the client can report.
// #ShutdownDrain is how long the drain pumps the loop at quit.
#ShutdownDrain: string & =~"^[0-9]+s$" | *"5s"

#HttpError: "connect" | "tls" | "alpn" | "timeout" | "cancelled" | "tooManyRedirects" | "headerCeiling" | "bodyCeiling" | "streamCeiling" | "protocol" | "closed"

// #Url is an absolute http(s) target.
#Url: string & =~"^https?://"

// #Headers is the header map, named so the shape is first class.
#Headers: [string]: string

// #Body is an opaque payload; the client never inspects it.
#Body: bytes

// #HttpRequest is one submission. `version` overrides ALPN and is test-only.
#HttpRequest: {
    method:   #HttpMethod
    url:      #Url
    headers?: #Headers
    body?:    #Body
    version?: #HttpVersion
}

// #HttpResponse is what a caller receives.
#HttpResponse: {
    status:  uint16 & >=100 & <=599
    version: #HttpVersion
    headers: #Headers
    body:    #Body
    hops:    uint8
}

