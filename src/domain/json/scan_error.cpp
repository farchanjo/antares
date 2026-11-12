#include "domain/json/scan_error.hpp"

namespace antares::domain::json {

std::string_view scanReasonName(ScanReason reason) noexcept
{
    switch (reason) {
    case ScanReason::unexpectedEnd:
        return "unexpected end of document";
    case ScanReason::unexpectedCharacter:
        return "unexpected character";
    case ScanReason::controlCharacter:
        return "raw control character in a string";
    case ScanReason::badEscape:
        return "unknown escape";
    case ScanReason::badUnicodeEscape:
        return "bad unicode escape";
    case ScanReason::loneSurrogate:
        return "lone surrogate";
    case ScanReason::unterminatedString:
        return "unterminated string";
    case ScanReason::trailingComma:
        return "trailing comma";
    case ScanReason::expectedMemberName:
        return "expected a member name";
    case ScanReason::expectedColon:
        return "expected ':'";
    case ScanReason::expectedSeparator:
        return "expected ',' or the closing delimiter";
    case ScanReason::depthCeiling:
        return "nesting ceiling reached";
    case ScanReason::trailingContent:
        return "trailing content after the document";
    case ScanReason::missingName:
        return "missing metadata.name";
    case ScanReason::refusedUid:
        return "invalid uid";
    case ScanReason::refusedName:
        return "invalid name";
    case ScanReason::refusedKind:
        return "invalid kind";
    case ScanReason::refusedVersion:
        return "invalid resource version";
    }
    return "invalid";
}

} // namespace antares::domain::json
