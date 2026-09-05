// Protocol.h is header-only by design: the tables are constants, and putting
// them in a translation unit would let them drift from the static_asserts that
// check them. This file exists so the library has a home for anything that
// needs a definition later, and to keep the build graph stable.
#include "egg/Protocol.h"

namespace egg {
static_assert(wireLength(kReportLarge) == kLargeLen);
static_assert(wireLength(kReportSmall) == kSmallLen);
static_assert(wireLength(0x00) == 0, "an unknown report id must have no length");
}  // namespace egg
