// DDD role: ValueObject

package schemas

// #Backoff shapes the delay between attempts (FR-015).
#Backoff: {
    baseDelayMs: uint32 & >0        | *100
    maxDelayMs:  uint32 & >0        | *2000
    factor:      number & >=1       | *2.0
    jitter:      number & >=0 & <=1 | *0.2
}

// #RetryConditions says which outcomes are worth repeating (FR-015).
#RetryConditions: {
    onConnect:          bool | *true
    onTimeout:          bool | *true
    honorRetryAfter:    bool | *true
    retryNonIdempotent: bool | *false
}

// #Retry bounds repeated attempts (FR-015).
#Retry: {
    maxAttempts: uint8 & >=1 & <=10 | *3
    backoff:     #Backoff | *{}
    conditions:  #RetryConditions | *{}
}
