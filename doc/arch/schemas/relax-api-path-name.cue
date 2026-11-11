// DDD role: ValueObject

package schemas

import "strings"

// #Segment is any name the API server accepts for metadata.name: a path
// segment, 1 to 253 bytes, no '/' and no '%', and neither '.' nor '..'.
#Segment: string & strings.MaxRunes(253) & =~"^[^/%]+$" & !="." & !=".."

// #NamespaceLabel is the stricter rule the server enforces for a namespace.
#NamespaceLabel: string & =~"^[a-z0-9]([-a-z0-9]{0,61}[a-z0-9])?$"

// #PathName is what the builder accepts: an object name, or a namespace name.
#PathName: #Segment | #NamespaceLabel

// #RefusalReason is the closed set a refusal reports.
#RefusalReason: "empty" | "tooLong" | "dot" | "dotdot" | "separator" | "escape"
