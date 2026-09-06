#include "egg/BootloaderEntry.h"
#include "egg/FlashCommands.h"

namespace egg::fw {

const char* describe(EntryResult r) {
    switch (r) {
        case EntryResult::EnteredAndConfirmed:
            return "entered the bootloader and confirmed its identity";
        case EntryResult::NoApplicationDevice:
            return "no application device to send to; nothing was sent";
        case EntryResult::SendFailed:
            return "the report never went out; nothing changed on the device";
        case EntryResult::NoReenumeration:
            return "acknowledged, but PID 0x1977 never appeared";
        case EntryResult::UnrecognisedIdentity:
            return "PID 0x1977 appeared but its identity was not recognised";
    }
    return "unknown";
}

namespace {

// All three fields, per §4.2. The PID alone is not an identity: it is one
// 16-bit number that a half-initialised device could present while being
// something we have never seen.
bool isKnownBootloader(const SeenDevice& d) {
    return d.productId     == kProductIdBootloader
        && d.releaseNumber == kBootloaderRelease
        && d.product       == kBootloaderProduct;
}

}  // namespace

EntryOutcome enterBootloaderAndConfirm(EntryEnv& env) {
    EntryOutcome o;

    // Preflight, and the ORDER here is the whole of it.
    //
    // The bootloader check comes FIRST. A device already in the bootloader has
    // no application interface, so requiring one first would report "no
    // application device" and, worse, would invite a caller to retry into a
    // state where A1 3A -- an APPLICATION command whose effect on a bootloader
    // is constrained by nothing we have ever read -- gets sent anyway. Caught
    // by the "already in the bootloader" test, which failed on the first run
    // for exactly this reason.
    {
        const auto before = env.enumerate();
        for (const auto& d : before) {
            if (d.productId == kProductIdBootloader) {
                o.sawBootloaderPid = true;
                o.seen   = d;
                o.result = isKnownBootloader(d) ? EntryResult::EnteredAndConfirmed
                                                : EntryResult::UnrecognisedIdentity;
                return o;
            }
        }
        // Otherwise there must be exactly one application device. Zero means
        // nothing to talk to; more than one means we cannot say which mouse we
        // would be switching, and switching the wrong one is not recoverable by
        // apologising afterwards.
        std::size_t apps = 0;
        for (const auto& d : before)
            if (d.productId == kProductIdApplication) ++apps;
        if (apps != 1) {
            o.result = EntryResult::NoApplicationDevice;
            return o;
        }
    }

    o.sent = enterBootloader();

    const unsigned t0 = env.nowMs();
    std::vector<std::uint8_t> reply;
    bool readOk = false;
    if (!env.exchange(o.sent, reply, readOk)) {
        o.result = EntryResult::SendFailed;
        return o;
    }
    o.ackMs     = env.nowMs() - t0;
    o.replyRead = readOk;
    if (readOk && reply.size() >= 2) o.status = reply[1];

    // From here the decision is the enumeration. Note what is NOT here: no
    // test of o.status. In capture 09 the reply's byte 0 was 0x00 where 08's
    // was 0xa1, for the identical command -- byte 0 is not stable and byte 1 is
    // not what the vendor consults either.
    const unsigned t1 = env.nowMs();
    for (unsigned waited = 0; waited <= kEntryPollCeilMs; waited += kEntryPollStepMs) {
        for (const auto& d : env.enumerate()) {
            if (d.productId != kProductIdBootloader) continue;
            o.sawBootloaderPid = true;
            o.seen             = d;
            o.reenumerateMs    = env.nowMs() - t1;
            o.result = isKnownBootloader(d) ? EntryResult::EnteredAndConfirmed
                                            : EntryResult::UnrecognisedIdentity;
            return o;
        }
        env.sleepMs(kEntryPollStepMs);
    }

    o.reenumerateMs = env.nowMs() - t1;
    o.result        = EntryResult::NoReenumeration;
    return o;
}

}  // namespace egg::fw
