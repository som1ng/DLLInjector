#include "dllinjector/result.h"

namespace dllinjector {
const char* CategoryName(ErrorCategory category) {
    switch (category) {
    case ErrorCategory::InvalidArgument: return "invalid_argument";
    case ErrorCategory::AccessDenied: return "access_denied";
    case ErrorCategory::NotFound: return "not_found";
    case ErrorCategory::Unsupported: return "unsupported";
    case ErrorCategory::Timeout: return "timeout";
    case ErrorCategory::Io: return "io";
    case ErrorCategory::MalformedPe: return "malformed_pe";
    default: return "system";
    }
}
int ExitCode(ErrorCategory category) {
    switch (category) {
    case ErrorCategory::MalformedPe: return 1;
    case ErrorCategory::InvalidArgument: return 2;
    case ErrorCategory::AccessDenied:
    case ErrorCategory::NotFound: return 3;
    case ErrorCategory::Timeout: return 5;
    default: return 4;
    }
}
}
