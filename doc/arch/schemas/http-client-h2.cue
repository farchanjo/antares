// DDD role: ValueObject

package schemas

// #HttpVersion is what ALPN negotiated. The caller never picks it by hand.
#HttpVersion: "h2" | "http/1.1"

// #HttpMethod covers what Kubernetes needs; extension methods stay strings.
#HttpMethod: "GET" | "POST" | "PUT" | "PATCH" | "DELETE" | "HEAD" | "OPTIONS"

// #HttpError is closed: every failure the client can report.
#HttpError:
    | "connect"
    | "tls"
    | "alpn"
    | "timeout"
    | "cancelled"
    | "reentrant"
    | "tooManyRedirects"
    | "headerCeiling"
    | "bodyCeiling"
    | "streamCeiling"
    | "protocol"
    | "closed"

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

// #Limits bounds every unbounded thing, so a hostile peer cannot allocate.
#Limits: {
    maxRedirects:        uint8 & >=0 & <=32   | *10
    maxStreamsPerConn:   uint16 & >0          | *100
    maxHeaderBytes:      uint32 & >0          | *32768
    maxBodyBytes:        uint64 & >0          | *67108864
    idleTimeoutMs:       uint32 & >0          | *15000
}

// #Auth is how a request proves identity (FR-013).
#Auth: {
    kind: #AuthKind | *"none"
    principal?:         string
    secret?:            string
    certificatePem?:    string
    privateKeyPem?:     string
}

#AuthKind: "none" | "bearer" | "basic" | "clientCertificate"

// #Pool bounds connection reuse (FR-011).
#Pool: {
    maxIdlePerHost: uint16 & >0 | *4
    maxIdleTotal:   uint16 & >0 | *32
    idleTimeoutMs:  uint32 & >0 | *15000
}

// #Timeouts are per-stage deadlines; zero disables one (FR-012).
#Timeouts: {
    connectMs: uint32 | *10000
    readMs:    uint32 | *30000
    writeMs:   uint32 | *30000
    requestMs: uint32 | *120000
}

// #ClientConfig is the single published configuration surface (FR-014).
#ClientConfig: {
    limits:                #Limits | *{}
    pool:                  #Pool | *{}
    timeouts:              #Timeouts | *{}
    auth:                  #Auth | *{kind: "none"}
    followRedirects:       bool | *true
    verifyPeer:            bool | *true
    forwardAuthOnRedirect: bool | *false
    userAgent:             string | *"antares/0.1"
}
