// DDD role: ValueObject

package schemas

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

// #ClientConfig is the single published configuration surface (FR-014).
#ClientConfig: {
    limits:     #Limits | *{}
    pool:       #Pool | *{}
    timeouts:   #Timeouts | *{}
    auth:       #Auth | *{}
    redirects:  #Redirects | *{}
    verifyPeer: bool | *true
    userAgent:  string | *"antares/0.1"
}
