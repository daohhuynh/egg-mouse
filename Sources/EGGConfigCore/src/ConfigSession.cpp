#include "egg/ConfigSession.h"

#include <cstring>

namespace egg::cfg {

const char* describe(Result r) {
    switch (r) {
        case Result::Ok:               return "ok";
        case Result::ReadFailed:       return "the device would not answer a read";
        case Result::ReadImplausible:  return "the device answered, but not with a settings record";
        case Result::AlreadySet:       return "already set; nothing written";
        case Result::RefusedSelfCheck: return "our own frame failed its self-check; nothing written";
        case Result::WriteRejected:    return "the device did not acknowledge the write";
        case Result::VerifyReadFailed: return "written, but the read-back failed: device state UNKNOWN";
        case Result::VerifyMismatch:   return "read back, and the byte we sent is not there";
    }
    return "?";
}

namespace {

// The ONLY sanctioned departure from a pure copy of what the device gave us,
// in one function so that "which bytes may we change?" has a single answer.
void applyUnknownPolicy(std::vector<std::uint8_t>& frame, UnknownBytes policy) {
    if (policy != UnknownBytes::MatchVendor) return;
    for (std::size_t r = kRecordUnknownFirst; r <= kRecordUnknownLast; ++r)
        frame[kPayloadOffset + r] = 0x00;
}

// True if a byte we changed relative to the read is one the policy allows us to
// change. Shared by both self-checks below.
bool policyPermits(const ByteChange& c, UnknownBytes policy) {
    return policy == UnknownBytes::MatchVendor &&
           c.recordOffset >= kRecordUnknownFirst &&
           c.recordOffset <= kRecordUnknownLast &&
           c.after == 0x00;
}

}  // namespace

bool ConfigSession::read(std::vector<std::uint8_t>& out, Result& result) {
    justStored_ = false;
    Reply r = link_.readRecord();
    if (!r.ok()) { result = Result::ReadFailed; return false; }
    if (!(log_ ? plausible(r.buf, *log_) : plausible(r.buf))) {
        result = Result::ReadImplausible; return false;
    }
    out = std::move(r.buf);
    result = Result::Ok;

    // AFTER validation and never before it. A vault filled from an implausible
    // read is worse than an empty one: it looks like an undo and is not.
    if (vault_) justStored_ = vault_->offer(out);
    return true;
}

std::vector<std::uint8_t> ConfigSession::buildFrame(
        const std::vector<std::uint8_t>& before, const Settable& f,
        std::uint8_t encodedValue, UnknownBytes policy) {
    // §4.1: never construct a settings blob from scratch, and never act on a
    // read that did not validate. A frame built from a bad read is precisely
    // the thing the rule forbids, so refuse rather than produce one.
    if (!plausible(before)) return {};

    std::vector<std::uint8_t> frame = Transport::frame(kReportLarge, kWriteSettings);
    if (frame.size() != kLargeLen) return {};

    // Read-modify-write: the payload starts as an exact copy of what the device
    // gave us. Every byte we do not understand travels back unchanged unless a
    // rule below says otherwise.
    std::memcpy(frame.data() + kPayloadOffset,
                before.data() + kPayloadOffset, kPayloadLen);

    const std::size_t at = kPayloadOffset + f.recordOffset;
    frame[at] = composeByte(before[at], f, encodedValue);

    // See ConfigRecord.h for why matching the vendor is the OBSERVED option and
    // preserving is the novel one.
    applyUnknownPolicy(frame, policy);

    return frame;
}

SetOutcome ConfigSession::set(const Settable& f, std::uint8_t encodedValue) {
    SetOutcome o;
    o.intendedOffset = f.recordOffset;

    // 1. Read, and validate. §4.1: "Never write after a failed read."
    if (!read(o.before, o.result)) return o;

    const std::size_t at = kPayloadOffset + f.recordOffset;
    const std::uint8_t want = composeByte(o.before[at], f, encodedValue);
    o.intendedValue = want;

    // 2. Build the frame we intend to send.
    std::vector<std::uint8_t> frame = buildFrame(o.before, f, encodedValue, policy_);
    if (frame.size() != kLargeLen) { o.result = Result::RefusedSelfCheck; return o; }

    // 3. SELF-CHECK, before anything goes out. Compare the frame we built
    //    against the record we read and require that the difference is exactly
    //    what we meant: the one field byte, plus record 0x01..0x04 if and only
    //    if the policy is MatchVendor. Anything else is a bug in us, and §2
    //    says the risk is bugs in our own code.
    o.weChanged = diff(o.before, frame);
    bool selfOk = true;
    bool sawField = (o.before[at] == want);   // a no-op field write shows no diff
    for (const ByteChange& c : o.weChanged) {
        if (c.recordOffset == f.recordOffset && c.after == want) { sawField = true; continue; }
        if (policyPermits(c, policy_)) continue;
        selfOk = false;                       // a byte moved that we did not intend
    }
    if (!selfOk || !sawField) { o.result = Result::RefusedSelfCheck; return o; }

    // 4. Nothing to do? Then send nothing. Checked AFTER the self-check so a
    //    no-op still proves the frame we would have sent was well formed.
    //
    //    THE TEST IS ON THE FIELD, not on the frame, and the difference matters
    //    once the policy is MatchVendor. Under it our frame legitimately differs
    //    from the read at record 0x01..0x04, so `weChanged` is non-empty even
    //    when the setting is already what the user asked for -- and writing on
    //    that basis would mean every redundant `set` puts a frame on the wire
    //    purely to normalise four bytes whose meaning we do not know. The policy
    //    shapes frames we were already sending; it is not a reason to send one.
    if (o.before[at] == want) { o.result = Result::AlreadySet; return o; }
    if (o.weChanged.empty())  { o.result = Result::AlreadySet; return o; }

    // 5. Write.
    o.sent = frame;
    o.wrote = true;
    Reply w = link_.writeRecord(frame);
    if (!w.ok()) { o.result = Result::WriteRejected; return o; }

    // 6. Read back and verify the DATA. §4.1: "Never let a reported success
    //    stand in for verifying the data itself." The acknowledgement in `w`
    //    is not evidence of anything and is deliberately not consulted again.
    Result rr = Result::Ok;
    if (!read(o.after, rr)) { o.result = Result::VerifyReadFailed; return o; }
    o.changed = diff(o.before, o.after);
    if (o.after[at] != want) { o.result = Result::VerifyMismatch; return o; }

    o.result = Result::Ok;
    return o;
}

// ---------------------------------------------------------------------------
// restore
// ---------------------------------------------------------------------------
std::vector<std::uint8_t> ConfigSession::buildRestoreFrame(
        const std::vector<std::uint8_t>& want, UnknownBytes policy) {
    // A saved record is held to the same bar as a fresh read. A file that is
    // not a plausible record is not a thing to put on the wire, whatever it
    // came from -- and a truncated or zeroed save is exactly what a failed
    // earlier session leaves behind.
    if (!plausible(want)) return {};

    std::vector<std::uint8_t> frame = Transport::frame(kReportLarge, kWriteSettings);
    if (frame.size() != kLargeLen) return {};

    std::memcpy(frame.data() + kPayloadOffset,
                want.data() + kPayloadOffset, kPayloadLen);
    applyUnknownPolicy(frame, policy);
    return frame;
}

RestoreOutcome ConfigSession::restore(const std::vector<std::uint8_t>& want) {
    RestoreOutcome o;

    // 1. Read, and validate. Not because we need the bytes -- we are about to
    //    overwrite them -- but because §4.1's rule is about not writing to a
    //    device that is not answering sensibly.
    if (!read(o.before, o.result)) return o;

    // 2. Build.
    std::vector<std::uint8_t> frame = buildRestoreFrame(want, policy_);
    if (frame.size() != kLargeLen) { o.result = Result::RefusedSelfCheck; return o; }

    // 3. SELF-CHECK: every payload byte we are about to send must be a byte the
    //    saved record supplied, except where the policy says otherwise. This is
    //    what makes restore safe to have at all -- it cannot introduce a value
    //    that no device ever produced.
    for (std::size_t i = kPayloadOffset; i < kPayloadOffset + kPayloadLen; ++i) {
        if (frame[i] == want[i]) continue;
        const ByteChange c{i - kPayloadOffset, want[i], frame[i]};
        if (policyPermits(c, policy_)) continue;
        o.result = Result::RefusedSelfCheck;
        return o;
    }

    o.incoming = diff(o.before, want);

    // 4. Write.
    o.sent  = frame;
    o.wrote = true;
    Reply w = link_.writeRecord(frame);
    if (!w.ok()) { o.result = Result::WriteRejected; return o; }

    // 5. Read back and compare against WHAT WE SENT, not against the file. Under
    //    MatchVendor those differ at record 0x01..0x04 by design, and checking
    //    against the file would report a mismatch the device did not make.
    Result rr = Result::Ok;
    if (!read(o.after, rr)) { o.result = Result::VerifyReadFailed; return o; }
    o.remaining = diff(frame, o.after);
    o.result = o.remaining.empty() ? Result::Ok : Result::VerifyMismatch;
    return o;
}

Result ConfigSession::factoryReset() {
    Reply r = link_.factoryReset();
    return r.ok() ? Result::Ok : Result::WriteRejected;
}

}  // namespace egg::cfg
