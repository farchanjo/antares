// DDD role: ValueObject

package schemas

// #Limits bounds every unbounded thing, so a hostile peer cannot allocate.
#Limits: {
    maxRedirects:        uint8 & >=0 & <=32   | *10
    maxStreamsPerConn:   uint16 & >0          | *100
    maxHeaderBytes:      uint32 & >0          | *32768
    maxBodyBytes:        uint64 & >0          | *67108864
}

// #Credential is any non-empty material: a principal, a secret, a PEM
// certificate chain or a PEM private key. Empty is the absence case.
#Credential: string & !=""

// #AuthKind is how a request proves identity (FR-013).
// #Auth carries the credential material.
#Auth: {
    kind: "none" | "bearer" | "basic" | "clientCertificate" | *"none"
    principal?:      #Credential
    secret?:         #Credential
    certificatePem?: #Credential
    privateKeyPem?:  #Credential
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

// #Retry bounds repeated attempts (FR-015).
#Retry: {
    maxAttempts: uint8 & >=1 & <=10 | *3
    // #Backoff delay shape, doubled per attempt.
    backoff: {
        baseDelayMs: uint32 & >0        | *100
        maxDelayMs:  uint32 & >0        | *2000
        factor:      number & >=1       | *2.0
        jitter:      number & >=0 & <=1 | *0.2
    } | *{}
    // #RetryConditions which outcomes are worth repeating.
    conditions: {
        onConnect:          bool | *true
        onTimeout:          bool | *true
        honorRetryAfter:    bool | *true
        retryNonIdempotent: bool | *false
    } | *{}
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
