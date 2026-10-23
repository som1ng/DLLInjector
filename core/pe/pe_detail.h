#pragma once
#include "dllinjector/pe.h"

namespace dllinjector::detail {
using PeClock = std::uint64_t (*)();
// Deterministic deadline seam; the public entry points use the system clock.
Result<PeInfo> ParsePe(const std::vector<std::uint8_t>& bytes, std::uint32_t timeout_ms, PeClock clock);
}
