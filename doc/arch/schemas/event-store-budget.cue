// DDD role: ValueObject

package schemas

import "strings"

// Budget and batch shapes for the event store (feature 005). Split from
// bound-the-event-memory.cue because that file would otherwise pass the
// one-definition ceiling.

// #EntryCeiling bounds one stored entry: a typical fixture object and the worst
// legal one (FR-003).
#EntryCeiling: {
    typical: uint32 & >0 | *512
    worst:   uint32 & >0 | *1024
}

// #ScopeCeiling bounds one scope: entries, and the label text inside an entry.
#ScopeCeiling: {
    entries:    uint32 & >0 | *50000
    labels:     uint32 & >0 | *16
    labelBytes: uint32 & >0 | *256
    fixedBytes: uint32 & >0 | *65536
}

// #Totals bounds the product across open scopes (FR-004).
#Totals: {
    scopes:  uint32 & >0 | *32
    entries: uint32 & >0 | *100000
    bytes:   uint64 & >0 | *104857600
}

// #StoreBudget is the whole budget. The two inequations are the proof and must
// hold for the defaults as written.
#StoreBudget: {
    entry:        #EntryCeiling
    scope:        #ScopeCeiling
    totals:       #Totals
    _scopeFits:   true & (scope.entries <= totals.entries)
    _budgetFits:  true & (totals.entries*entry.worst + totals.scopes*scope.fixedBytes <= totals.bytes)
}

// #Scope is what a store scope is keyed by, plus the kind its resource serves.
// One group-version-resource serves exactly one kind, so the kind belongs to
// the scope and not to every entry.
#Scope: {
    cluster:              #Text
    groupVersionResource: #Text & strings.MaxRunes(316)
    kind:                 #Text
    namespace:            #NamespaceName
}

// #ScopeState is what a scope publishes about itself.
#ScopeState: {
    stale:     bool | *false
    truncated: bool | *false
    refused:   uint64 | *0
}

// #RowRange is a half-open row range against a stated row order.
#RowRange: {
    first: uint32
    last:  uint32 & >=first
}

// #ChangeSet is one batch in row terms, applied by a list model one to one.
// `fields` is the closed mask a repaint reads.
#ChangeSet: {
    removed:  [...#RowRange]
    inserted: [...#RowRange]
    changed:  [...{row: uint32, fields: [...("resourceVersion" | "generation" | "labels" | "status")] & list.MinItems(1)}]
}

// #BoundTheEventMemory is the root: a budget, the scope state, and its rows.
// It lives here so `bound-the-event-memory.cue` stays under the definition
// ceiling; the reference to #Entry is the only cross-file edge.
#BoundTheEventMemory: {
    budget:  #StoreBudget
    scope:   #ScopeState
    entries: [#Entry, ...#Entry]
}
