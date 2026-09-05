#include "egg/RecordVault.h"

#include "egg/ConfigRecord.h"

#include <fstream>
#include <iterator>
#include <vector>

namespace egg::cfg {

namespace {

// True if `path` already holds a record that could actually be USED as an undo.
//
// This was a length test alone, and a length test is not enough. Both tools
// report "settings undo present" on the strength of this function -- egg-flash
// before a flash, egg-config after every read -- while the loader that has to
// read the file back applies plausible(). The two halves of the vault disagreed
// about the file format, so a 1041-byte file whose payload was uniform (what an
// interrupted session leaves behind) was announced as an undo and would then be
// refused at the moment it was needed. Found by adversarial audit, 2026-09-05,
// alongside the byte-0 defect that made EVERY saved record unloadable.
//
// Now it answers the question the callers actually ask: is this loadable? The
// original reasoning still applies and is strengthened -- a file that is not a
// usable known-good blob must not suppress the save that replaces it.
bool alreadyGood(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<std::uint8_t> buf(
        (std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return plausible(buf);
}

}  // namespace

FileRecordVault::FileRecordVault(std::string path) : path_(std::move(path)) {
    holds_ = alreadyGood(path_);
}

bool FileRecordVault::offer(const std::vector<std::uint8_t>& record) {
    err_.clear();
    if (holds_) return false;                      // never overwrite

    // The caller is supposed to hand us only validated records. Check anyway:
    // this file exists so that a bad session cannot destroy a good record, and
    // trusting the caller would be exactly the assumption that breaks that.
    if (!plausible(record)) {
        err_ = "the record offered was not structurally plausible";
        return false;
    }

    std::ofstream f(path_, std::ios::binary);
    if (!f) { err_ = "could not open " + path_ + " for writing"; return false; }
    f.write(reinterpret_cast<const char*>(record.data()),
            static_cast<std::streamsize>(record.size()));
    f.close();

    // Confirm from the filesystem rather than from the stream's own opinion.
    // §4.1's rule about not letting a reported success stand in for verifying
    // the data applies to our own disk writes too.
    if (!alreadyGood(path_)) {
        err_ = "wrote " + path_ + " but it did not come back as a usable record";
        return false;
    }
    holds_ = true;
    return true;
}

}  // namespace egg::cfg
