// DDD role: ValueObject

package schemas

// #Text is any non-empty stored string: a uid, a resource version, a kind.
#Text: string & !=""

// #Name is a DNS-1123 object name; empty means cluster-scoped.
#Name: string & =~"^([a-z0-9]([-a-z0-9]*[a-z0-9])?)?$"

// #ObjectKey is the scope plus the uid: the store's only index.
#ObjectKey: {
    groupVersionResource: #Text
    namespace:            #Name
    uid:                  #Text
}

// #Flag is a switch the store publishes as state.
#Flag: bool

// #StatusSummary is the bounded part of status a view shows. Never the whole
// status document, so a hostile object cannot inflate an entry (TM-17).
#StatusSummary: {
    phase?: string & =~"^(Pending|Running|Succeeded|Failed|Unknown)?$"
    ready?: #Flag
    restarts?: uint32 & <=100000
    reason?: string & *"" & =~"^([A-Za-z][A-Za-z0-9]*)?$"
}

// #Identity is what names an object to a view.
#Identity: {
    uid:       #Text
    name:      #Name
    namespace: #Name
    kind:      #Text
}

// #Entry is one stored object. Its shape is a contract: adding a field is
// additive (D-16), and managedFields never appears here (FR-002).
#Entry: {
    identity:        #Identity
    resourceVersion: #Text
    generation:      uint64
    status:          #StatusSummary | *{}
    updatedAtTick:   uint64
}

// #ScopeBudget bounds one scope's retention (FR-003, FR-004).
#ScopeBudget: {
    maxEntries:    uint32 & >0 | *20000
    maxEntryBytes: uint32 & >0 | *512
}

// #BoundTheEventMemory is what the feature publishes.
#BoundTheEventMemory: {
    budget:  #ScopeBudget
    entries: [...#Entry]
}
