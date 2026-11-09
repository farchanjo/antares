// DDD role: ValueObject

package schemas

// #IoError is the closed failure set of the IoEngine port (ADR 0007).
// A case is added only with a scenario that reaches it.
#IoError: "wouldBlock" | "closed" | "cancelled" | "tooManyDescriptors" | "system"

// #IoOperation names one operation the port declares.
#IoOperation: "submitRead" | "submitWrite" | "submitAccept" | "submitConnect" | "post" | "run" | "stop"

// #HandleKind is the classification IoHandle assigns at adoption (ADR 0008).
// A file completes through the immediate strategy; a stream waits on readiness.
#HandleKind: "stream" | "file" | "other"

// #IoBackend names a selectable backend. kqueue is the macOS production
// backend; poll is the portable reference the conformance suite compares against.
#IoBackend: "kqueue" | "poll"

// #IoEngine is the port surface fixed by ADR 0006 and shaped by ADR 0007.
#IoEngine: {
    name:       #IoBackend
    operations: [...#IoOperation]
    errors: [...#IoError]
    kinds:  [...#HandleKind]
}
