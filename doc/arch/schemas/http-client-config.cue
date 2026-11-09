// DDD role: ValueObject

package schemas

// #Limits bounds every unbounded thing, so a hostile peer cannot allocate.
#Limits: {
    maxRedirects:        uint8 & >=0 & <=32   | *10
    maxStreamsPerConn:   uint16 & >0          | *100
    maxHeaderBytes:      uint32 & >0          | *32768
    maxBodyBytes:        uint64 & >0          | *67108864
    idleTimeoutMs:       uint32 & >0          | *15000
}

// #AuthKind is how a request proves identity (FR-013).
#AuthKind: "none" | "bearer" | "basic" | "clientCertificate"

// #Auth carries the credential material.
#Auth: {
    kind: #AuthKind | *"none"
    principal?:      string
    secret?:         string
    certificatePem?: string
    privateKeyPem?:  string
}

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

// #Redirects is the hop policy (FR-006).
#Redirects: {
    follow:          bool | *true
    forwardAuth:     bool | *false
    maxHops:         uint8 & <=32 | *10
}

// #Transport groups what shapes the wire rather than a single request.
#Transport: {
    pool:       #Pool | *{}
    timeouts:   #Timeouts | *{}
    redirects:  #Redirects | *{}
    retry:      #Retry | *{}
    verifyPeer: bool | *true
}

// #ClientConfig is the single published configuration surface (FR-014).
#ClientConfig: {
    limits:    #Limits | *{}
    transport: #Transport | *{}
    auth:      #Auth | *{}
    userAgent: string | *"antares/0.1"
}
