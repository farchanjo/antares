// DDD role: ValueObject

package schemas

// #Offset is a byte position in the document being scanned.
#Offset: uint32

// #ScanFailure says where a scan stopped and which rule broke.
#ScanFailure: {
    offset: #Offset
    reason: #ScanReason
}

// #ScanReason is the closed set of ways a scan fails.
#ScanReason: "unexpected-end" | "bad-delimiter" | "bad-escape" | "too-deep" | "bad-surrogate"

// #ScanBounds is what the scanner will not exceed.
#ScanBounds: {
    maxDepth:     uint32 & >0 | *32
    maxEntryBytes: uint32 & >0 | *1024
}

// #ScanResult is one scan: the entry, or where it stopped.
#ScanResult: {
    bounds: #ScanBounds
    failure?: #ScanFailure
}
