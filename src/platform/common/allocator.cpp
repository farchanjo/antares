// Routes every C++ allocation through mimalloc (P-14).
//
// One translation unit, included nowhere else: the override headers redefine
// `malloc` and `operator new` for the whole link.

#include <mimalloc-override.h>
