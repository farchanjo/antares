// DDD role: ValueObject

package schemas

// #EngineBackend is the backend the factory actually built. Never branched on
// above the factory; it exists for the log and the conformance report (FR-007).
#EngineBackend: "kqueue" | "io_uring" | "epoll" | "poll"

// #CompletionCounters move once per op, whatever settled it (FR-008).
#CompletionCounters: {
    submitted: uint64
    completed: uint64
    failed: uint64
}

// #Flag is a switch the engine publishes as state.
#Flag: bool

// #LoopState is the stop flag and whether the loop may serve again (FR-006).
#LoopState: {
    stopped:  #Flag
    runnable: #Flag
}

// #HardenTheEngineLoop is what the feature publishes: the truthful backend name
// and counters that do not undercount cancelled ops.
#HardenTheEngineLoop: {
    backend:  #EngineBackend
    counters: #CompletionCounters
    loop:     #LoopState
}
