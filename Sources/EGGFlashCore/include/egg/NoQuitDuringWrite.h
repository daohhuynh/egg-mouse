// NoQuitDuringWrite.h -- the erase-through-verified-image unit cannot be asked
// to stop.
//
// engineering-rules.md §3: "Trap SIGINT/SIGTERM explicitly during the write phase."
//
// NEVER IMPLEMENTED UNTIL 2026-09-05, when an adversarial audit noticed the
// rule had no code behind it. It is not hypothetical: earlier the same day a
// command sat silent long enough that interrupting it looked like the obvious
// move -- of a read-only command, where it would have been fine. During the write
// phase the answer is no, and the difference cannot be left to a judgement call
// made under stress.
//
// §4.2: after the erase the device has no valid application, so exiting cleanly
// GUARANTEES the bad outcome. A default-disposition Ctrl-C terminates the
// process between two chunks and leaves exactly that.
//
// SIG_IGN, not a flag a loop polls: the loops in WritePhase.cpp are
// deliberately unbounded, so a "please stop" flag would either be ignored
// anyway or become the early exit §4.2 forbids. The dispositions are installed
// for the erase-through-verify unit only and restored immediately after, so
// before the erase Ctrl-C still works normally -- which is correct, because
// before the erase abort is always right.
//
// This is not an inescapable process. SIGKILL cannot be trapped, so `kill -9`
// still works for a genuinely wedged tool. It is not offered in the message,
// because someone reaching for it should have to decide to.
//
// ---------------------------------------------------------------------------
// SIGPIPE, added 2026-09-06, and it was a real hole
// ---------------------------------------------------------------------------
// The class ignored SIGINT, SIGTERM and SIGHUP and nothing else. But the write
// phase WRITES: `link.note()` puts progress and every recovery instruction on
// stderr, and the CLI prints to stdout around it. If either stream is a pipe
// whose reader goes away -- `egg-flash flash ... | head`, a `tee` that is
// killed, a terminal pane that closes, a GUI that stopped reading -- the next
// write raises SIGPIPE, and SIGPIPE's DEFAULT ACTION TERMINATES THE PROCESS.
//
// That is §4.2's forbidden outcome reached by the one route the guard did not
// cover, and it is worse than the ones it did: nobody typed anything. It also
// gets more likely the more the phase says, which is the direction per-block
// progress moves it.
//
// With SIGPIPE ignored, `write` returns EPIPE instead and the phase carries on
// driving the device with its output going nowhere. That is the correct
// trade: the mouse matters and the log does not.
//
// ---------------------------------------------------------------------------
// WHY IT LIVES IN EGGFlashCore AND NOT IN main.cpp
// ---------------------------------------------------------------------------
// It used to be a class in `Sources/egg-flash/main.cpp`, which meant NOTHING
// COULD TEST IT -- not the unit tests, not the mock, nothing. The SIGPIPE hole
// sat there because the only thing that ever read the code was a person. It is
// a library type now, and Tests/test_flash.cpp raises each signal at it and
// requires the test process to survive and to get its previous dispositions
// back afterwards. That test can fail, which is the whole point (§6.2).
#pragma once

#include <csignal>
#include <cstddef>

namespace egg::fw {

class NoQuitDuringWrite {
public:
    NoQuitDuringWrite();
    ~NoQuitDuringWrite();
    NoQuitDuringWrite(const NoQuitDuringWrite&) = delete;
    NoQuitDuringWrite& operator=(const NoQuitDuringWrite&) = delete;

    // The signals this guard covers, for a test to enumerate rather than
    // hardcode a list that could drift from the constructor.
    static const int* signals(std::size_t& count);

private:
    // One saved disposition per entry in signals(), in the same order.
    void (*prev_[4])(int) = {SIG_ERR, SIG_ERR, SIG_ERR, SIG_ERR};
};

}  // namespace egg::fw
