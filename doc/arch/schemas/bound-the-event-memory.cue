// DDD role: ValueObject

package schemas

import (
    "list"
    "strings"
    "time"
)

// #Text is any non-empty stored string: a uid, a resource version, a kind.
#Text: string & !="" & strings.MaxRunes(64)

// #NamespaceName is a DNS-1123 label; empty means cluster-scoped.
#NamespaceName: string & =~"^([a-z0-9]([-a-z0-9]{0,61}[a-z0-9])?)?$"

// #ObjectName is any name the API server accepts; the store bounds it and never
// validates more strictly than the server.
#ObjectName: string & =~"^[^/%]{1,253}$" & !="." & !=".."

// #ObjectKey is the store's only index: the scope plus the uid.
#ObjectKey: {
    cluster:              #Text
    groupVersionResource: #Text & strings.MaxRunes(316)
    namespace:            #NamespaceName
    uid:                  #Text
}

// #Identity is what a view renders to name an object.
#Identity: {
    uid:       #Text
    name:      #ObjectName
    namespace: #NamespaceName
    kind:      #Text
}

// #Labels keeps labels in key order until either ceiling is reached (TM-17).
// Keys are qualified label keys and values are label values, both inline.
#Labels: {
    items: [...{key:    string & strings.MaxRunes(317) & =~"^([a-z0-9]([-a-z0-9.]*[a-z0-9])?/)?[A-Za-z0-9]([-A-Za-z0-9_.]*[A-Za-z0-9])?$",
                value: string & strings.MaxRunes(63) & =~"^([A-Za-z0-9]([-A-Za-z0-9_.]*[A-Za-z0-9])?)?$"}] & list.MaxItems(16)
    truncated: bool | *false
    _bytes:    list.Sum([for l in items {len(l.key) + len(l.value)}]) & <=256
}

// #StatusSummary is the bounded part of status a view renders.
#StatusSummary: {
    reason?: string & strings.MaxRunes(128)
    ready?:  uint32
    total?:  uint32
}

// #Entry is one stored object. Additive only (D-16); managedFields and
// annotations never appear here (FR-002).
#Entry: {
    identity:          #Identity
    resourceVersion:   #Text
    generation:        uint64
    creationTimestamp: string & time.Time()
    labels:            #Labels
    status:            #StatusSummary | *{}
}
