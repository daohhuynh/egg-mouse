#include "egg/NoQuitDuringWrite.h"

#include <cstddef>

namespace egg::fw {

namespace {
// SIGINT and SIGTERM are engineering-rules.md §3's own list. SIGHUP because closing the
// terminal is the same mistake with less deliberation behind it than Ctrl-C.
// SIGPIPE because the phase writes and a dead reader must not kill it -- see
// the header.
const int kSignals[] = {SIGINT, SIGTERM, SIGHUP, SIGPIPE};
constexpr std::size_t kCount = sizeof kSignals / sizeof kSignals[0];
}  // namespace

const int* NoQuitDuringWrite::signals(std::size_t& count) {
    count = kCount;
    return kSignals;
}

NoQuitDuringWrite::NoQuitDuringWrite() {
    static_assert(kCount <= sizeof prev_ / sizeof prev_[0],
                  "prev_ is too small for kSignals; grow it in the header");
    for (std::size_t i = 0; i < kCount; ++i)
        prev_[i] = std::signal(kSignals[i], SIG_IGN);
}

NoQuitDuringWrite::~NoQuitDuringWrite() {
    // Restore in REVERSE, and only what was saved. SIG_ERR means the install
    // failed, and passing SIG_ERR back to signal() would be undefined -- the
    // original code guarded this and the guard is kept.
    for (std::size_t i = kCount; i-- > 0;)
        if (prev_[i] != SIG_ERR) std::signal(kSignals[i], prev_[i]);
}

}  // namespace egg::fw
