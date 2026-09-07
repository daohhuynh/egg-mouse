// RecordVault.h -- §4.1: "Save a known-good blob to disk on first connect."
//
// That line was in engineering-rules.md from the beginning and was not implemented. What
// existed was `egg-config read --save F`, which is a different thing: it saves
// only when the user remembers to ask, and the moment it matters most is the
// one time nobody thought to ask.
//
// WHY IT MATTERS MORE THAN IT LOOKS. working-memory.md's open gaps carry two
// findings that together make this load-bearing:
//
//   - `A1 13` is BOTH the config tool's Factory Reset and the updater's last
//     command. If it means the same in both places, flashing wipes every
//     setting on the mouse. That is [G] and untestable without the device, so
//     the tool has to behave as if it were true.
//   - There is no evidence any config write persists across a power cycle, and
//     no persist command has been seen. So a record we failed to save may not
//     be recoverable by asking the device again.
//
// The undo for both is a file on disk that we did not have to remember to
// write.
//
// THE ONE RULE: keep the FIRST plausible record and never overwrite it. A vault
// that updates itself is not a known-good blob, it is a mirror of the current
// state -- and it would faithfully overwrite the good record with whatever a
// half-broken session left on the device.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace egg::cfg {

class RecordVault {
public:
    virtual ~RecordVault() = default;

    // Offered every record that passed plausible(). Implementations MUST ignore
    // the offer if they already hold one.
    //
    // Returns true if this call is what stored it, so a caller can say so once
    // rather than on every read. Never throws and never fails the operation it
    // is part of: a vault that could abort a read would be a new way to lose
    // access to the device, which is the opposite of the point.
    virtual bool offer(const std::vector<std::uint8_t>& record) = 0;

    virtual bool holds() const = 0;

    // Where the blob went, for printing. Empty if nothing was stored.
    virtual std::string where() const = 0;
};

// Writes to `path` if and only if no readable 1041-byte file is already there.
//
// The existence check is deliberately a READ of the existing file rather than a
// stat: a zero-length or truncated file left by an interrupted earlier run must
// not count as "we already have one".
class FileRecordVault : public RecordVault {
public:
    explicit FileRecordVault(std::string path);

    bool offer(const std::vector<std::uint8_t>& record) override;
    bool holds() const override { return holds_; }
    std::string where() const override { return holds_ ? path_ : std::string(); }

    // Why the last offer did not store anything, or "" if it did or if one was
    // already held. Surfaced so a failure to save is visible rather than a
    // silently absent file.
    const std::string& lastError() const { return err_; }

private:
    std::string path_;
    bool        holds_{false};
    std::string err_;
};

// For tests, and for a caller that wants the behaviour without touching disk.
class MemoryRecordVault : public RecordVault {
public:
    bool offer(const std::vector<std::uint8_t>& record) override {
        ++offers_;
        if (holds_) return false;
        kept_ = record;
        holds_ = true;
        return true;
    }
    bool holds() const override { return holds_; }
    std::string where() const override { return holds_ ? "(memory)" : std::string(); }

    const std::vector<std::uint8_t>& kept() const { return kept_; }
    unsigned offers() const { return offers_; }

private:
    std::vector<std::uint8_t> kept_;
    bool     holds_{false};
    unsigned offers_{0};
};

}  // namespace egg::cfg
