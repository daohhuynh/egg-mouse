#include "egg/RecordVault.h"

#include "egg/ConfigRecord.h"

#include <fstream>

namespace egg::cfg {

namespace {

// True if `path` already holds a full-length record. Read, not stat: a
// truncated file from an interrupted run is not a known-good blob, and treating
// it as one would suppress the save that replaces it.
bool alreadyGood(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff n = f.tellg();
    return n == static_cast<std::streamoff>(kLargeLen);
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
        err_ = "wrote " + path_ + " but it did not come back the right size";
        return false;
    }
    holds_ = true;
    return true;
}

}  // namespace egg::cfg
