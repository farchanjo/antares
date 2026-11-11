// DDD role: ValueObject

package schemas

// #StartupError is why the process could not build itself.
#StartupError: "engine" | "kubeconfig" | "credential" | "tls" | "window"

// #StartupOutcome is the result of build(): nothing, or the first failure.
#StartupOutcome: {
    error?: #StartupError
}

// #ShutdownSource is what asked the process to stop.
#ShutdownSource: "close" | "sigterm" | "sigint"

// #ShutdownStep is one step of the reverse order, named so a case can assert it.
#ShutdownStep: "handler" | "drain" | "window" | "model" | "store" | "client" | "engine"

// #Drained is whether the drain reached zero before its deadline.
#Drained: "yes" | "expired" | "skipped"

// #Shutdown is the record of one teardown.
#Shutdown: {
    source:  #ShutdownSource
    drained: #Drained
    ran:     [...#ShutdownStep]
}
