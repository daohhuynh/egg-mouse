// ConfigSession.h -- the config write path, in one place, small enough to read.
//
// §4.3: "Keep the write phase small and in one file. ~100 lines in one place
// can be audited by eye. Spread across eight files with clever abstractions, it
// cannot." That was honoured for the flasher and not for the config tool, whose
// write path was 100 lines inside a CLI command handler mixed with argument
// parsing and printf. Same size, unauditable, untestable.
//
// Every safety property §4.1 asks for is enforced HERE, once, and asserted by
// Tests/test_config.cpp over randomised runs against a device that lies:
//
//   - read-modify-write always; a settings blob is never constructed
//   - a read must be structurally plausible before anything acts on it
//   - never write after a failed read
//   - exactly the intended bytes differ from what was read, checked before the
//     frame goes out, not asserted in a comment
//   - read back after every write and verify the DATA, never the ack
#pragma once

#include "egg/ConfigLink.h"
#include "egg/ConfigRecord.h"

#include <cstdint>
#include <string>
#include <vector>

namespace egg::cfg {

// What a session operation actually produced. Every failure mode the mock can
// inject has its own value, so a test can assert which one happened rather
// than only that something did.
enum class Result {
    Ok,
    ReadFailed,          // the device would not answer, or answered badly
    ReadImplausible,     // it answered, and the record was not a record
    AlreadySet,          // nothing to do; no frame was sent
    RefusedSelfCheck,    // OUR frame differed from what we intended -- a bug in us
    WriteRejected,       // the device did not acknowledge the write
    VerifyReadFailed,    // wrote, then could not read back: state UNKNOWN
    VerifyMismatch,      // read back, and the byte we sent is not there
};

const char* describe(Result r);

// The outcome of one set operation, including everything a caller needs to
// report without re-reading the device.
struct SetOutcome {
    Result                   result{Result::ReadFailed};
    std::vector<std::uint8_t> before;      // the validated read, if we got one
    std::vector<std::uint8_t> sent;        // the exact frame, if we built one
    std::vector<std::uint8_t> after;       // the verifying read, if we got one
    std::vector<ByteChange>   changed;     // before -> after, device's own view
    std::size_t              intendedOffset{0};
    std::uint8_t             intendedValue{0};
    // Bytes our frame changed relative to the read. Under MatchVendor this
    // legitimately includes record 0x01..0x04; under Preserve it must not.
    std::vector<ByteChange>  weChanged;
    bool wrote{false};                     // did any frame reach the link?
};

// The outcome of a restore: writing back a whole record the DEVICE produced
// earlier. Separate from SetOutcome because the two differ in exactly the way
// that matters -- a set moves one byte and anything else is a bug, a restore
// moves as many as the saved record differs by and that is the point.
struct RestoreOutcome {
    Result                    result{Result::ReadFailed};
    std::vector<std::uint8_t> before;    // what the device holds now
    std::vector<std::uint8_t> sent;      // the exact frame, if we built one
    std::vector<std::uint8_t> after;     // the verifying read, if we got one
    std::vector<ByteChange>   incoming;  // device now -> the saved record
    std::vector<ByteChange>   remaining; // what we sent -> what came back
    bool wrote{false};
};

class ConfigSession {
public:
    // `log`, if given, receives the plausibility diagnostics -- the distinct
    // byte count on a good read, the reason on a bad one. Optional because the
    // tests want the verdict and not the narration, and because a session that
    // needs a Log to behave correctly would be a session whose safety depended
    // on its output.
    ConfigSession(ConfigLink& link, UnknownBytes policy, Log* log = nullptr)
        : link_(link), policy_(policy), log_(log) {}

    UnknownBytes policy() const { return policy_; }

    // A validated read. Returns false on either a transport failure or an
    // implausible record, and says which via `result`.
    bool read(std::vector<std::uint8_t>& out, Result& result);

    // Read, modify one field, self-check, write, read back, verify.
    SetOutcome set(const Settable& f, std::uint8_t encodedValue);

    // Build the frame that `set` would send, without a device. This is the
    // dry-run seam (§4.3) and the thing Tests/test_config_replay.py scores
    // against the vendor's own captured writes.
    //
    // Returns an empty vector if `before` is not a plausible record, because a
    // frame built from a bad read is exactly what §4.1 forbids.
    static std::vector<std::uint8_t> buildFrame(const std::vector<std::uint8_t>& before,
                                                const Settable& f,
                                                std::uint8_t encodedValue,
                                                UnknownBytes policy);

    // Write a whole record back. Still a read-modify-write in the sense that
    // matters: every byte came off this device, none was composed by us.
    //
    // The read still happens and must still be plausible, because §4.1's "never
    // write after a failed read" is not about needing the data -- it is about
    // not writing to a device that is not answering sensibly.
    RestoreOutcome restore(const std::vector<std::uint8_t>& want);

    // The frame `restore` would send, without a device.
    static std::vector<std::uint8_t> buildRestoreFrame(const std::vector<std::uint8_t>& want,
                                                       UnknownBytes policy);

    // A1 13. Separate from set() because it is not a read-modify-write: it
    // discards state by design, so none of the RMW guarantees apply and
    // pretending otherwise would be dishonest.
    Result factoryReset();

private:
    ConfigLink&  link_;
    UnknownBytes policy_;
    Log*         log_;
};

}  // namespace egg::cfg
