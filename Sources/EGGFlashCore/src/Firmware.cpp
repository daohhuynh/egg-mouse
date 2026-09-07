#include "egg/Firmware.h"

#include "egg/Provenance.h"

#include <CommonCrypto/CommonDigest.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace egg::fw {
namespace {

// Little-endian reads that refuse to run off the end. A malformed PE is a file
// we were handed, so every field access is bounds-checked rather than trusted;
// the alternative is a crash on a truncated download.
class Reader {
public:
    Reader(const std::vector<std::uint8_t>& b) : b_(b) {}
    bool u16(std::size_t off, std::uint16_t& v) const {
        if (off + 2 > b_.size()) return false;
        v = static_cast<std::uint16_t>(b_[off] | (b_[off + 1] << 8));
        return true;
    }
    bool u32(std::size_t off, std::uint32_t& v) const {
        if (off + 4 > b_.size()) return false;
        v = static_cast<std::uint32_t>(b_[off]) |
            (static_cast<std::uint32_t>(b_[off + 1]) << 8) |
            (static_cast<std::uint32_t>(b_[off + 2]) << 16) |
            (static_cast<std::uint32_t>(b_[off + 3]) << 24);
        return true;
    }
private:
    const std::vector<std::uint8_t>& b_;
};

struct Section {
    std::uint32_t va, vsize, raw, rawsize;
};

// One resource directory level. Entries are 8 bytes: name/id, then an offset
// whose top bit means "another directory" rather than a leaf.
constexpr std::uint32_t kDirBit = 0x80000000u;

bool readFile(const std::string& path, std::vector<std::uint8_t>& out,
              std::string& error) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { error = "cannot open " + path; return false; }
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) { std::fclose(f); error = "empty file: " + path; return false; }
    out.resize(static_cast<std::size_t>(n));
    std::size_t got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    if (got != out.size()) { error = "short read on " + path; return false; }
    return true;
}

}  // namespace

std::string sha256Hex(const std::uint8_t* data, std::size_t n) {
    unsigned char d[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data, static_cast<CC_LONG>(n), d);
    char out[2 * CC_SHA256_DIGEST_LENGTH + 1];
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; ++i)
        std::snprintf(out + 2 * i, 3, "%02x", d[i]);
    return std::string(out, 2 * CC_SHA256_DIGEST_LENGTH);
}

std::uint32_t wholeImageChecksum(const std::vector<std::uint8_t>& image) {
    // [D] FUN_00403580. A plain 32-bit sum of every byte; see Firmware.h for
    // why the vendor's four accumulators are one sum and not four.
    std::uint32_t sum = 0;
    for (std::uint8_t b : image) sum += b;
    return sum;
}

std::uint16_t blockChecksum(const std::uint8_t* block, std::size_t n) {
    // [D] §3.3, the loop at 0x401a00..0x401a34. Sixteen bits, and the
    // truncation is the point -- a 32-bit sum here would put the wrong two
    // bytes at [4..5] of every write frame.
    std::uint16_t sum = 0;
    for (std::size_t i = 0; i < n; ++i)
        sum = static_cast<std::uint16_t>(sum + block[i]);
    return sum;
}

bool extractResource(const std::string& exePath, const char* type,
                     std::uint16_t name, std::vector<std::uint8_t>& out,
                     std::string& error) {
    std::vector<std::uint8_t> f;
    if (!readFile(exePath, f, error)) return false;
    Reader r(f);

    std::uint16_t mz = 0;
    std::uint32_t peoff = 0;
    if (!r.u16(0, mz) || mz != 0x5A4D) { error = "not a PE file (no MZ)"; return false; }
    if (!r.u32(0x3C, peoff)) { error = "truncated DOS header"; return false; }
    std::uint32_t sig = 0;
    if (!r.u32(peoff, sig) || sig != 0x00004550) { error = "no PE signature"; return false; }

    std::uint16_t nsec = 0, optsize = 0;
    if (!r.u16(peoff + 6, nsec) || !r.u16(peoff + 20, optsize)) {
        error = "truncated COFF header"; return false;
    }
    std::uint16_t magic = 0;
    if (!r.u16(peoff + 24, magic)) { error = "truncated optional header"; return false; }
    // PE32 = 0x10b, PE32+ = 0x20b. The data directory sits at a different
    // offset in each; these binaries are PE32 but handle both rather than
    // silently mis-parse a 64-bit build.
    std::size_t ddir = peoff + 24 + (magic == 0x20b ? 112 : 96);
    std::uint32_t rsrcRva = 0;
    if (!r.u32(ddir + 2 * 8, rsrcRva)) { error = "no resource data directory"; return false; }
    if (!rsrcRva) { error = "binary has no resource section"; return false; }

    std::vector<Section> secs;
    std::size_t st = peoff + 24 + optsize;
    for (std::uint16_t i = 0; i < nsec; ++i) {
        std::size_t o = st + i * 40u;
        Section s{};
        if (!r.u32(o + 12, s.va) || !r.u32(o + 8, s.vsize) ||
            !r.u32(o + 20, s.raw) || !r.u32(o + 16, s.rawsize)) {
            error = "truncated section table"; return false;
        }
        secs.push_back(s);
    }
    auto toFile = [&](std::uint32_t rva, std::size_t& off) {
        for (const Section& s : secs) {
            std::uint32_t span = s.vsize ? s.vsize : s.rawsize;
            if (rva >= s.va && rva < s.va + span) {
                off = s.raw + (rva - s.va);
                return off < f.size();
            }
        }
        return false;
    };

    std::size_t rsrcBase = 0;
    if (!toFile(rsrcRva, rsrcBase)) { error = "resource RVA is not in any section"; return false; }

    // Walk type -> name -> language. At each level entries are sorted named
    // first then id, so we scan rather than binary-search: 74 leaves is nothing
    // and a scan cannot mis-order.
    auto entries = [&](std::size_t dir, std::size_t& first, std::uint32_t& n) {
        std::uint16_t nn = 0, ni = 0;
        if (!r.u16(dir + 12, nn) || !r.u16(dir + 14, ni)) return false;
        first = dir + 16;
        n = static_cast<std::uint32_t>(nn) + ni;
        return true;
    };
    auto entryName = [&](std::size_t e, std::uint32_t& id, std::string& s) {
        std::uint32_t v = 0;
        if (!r.u32(e, v)) return false;
        if (v & kDirBit) {                       // string name, UTF-16LE
            std::size_t p = rsrcBase + (v & ~kDirBit);
            std::uint16_t len = 0;
            if (!r.u16(p, len)) return false;
            s.clear();
            for (std::uint16_t i = 0; i < len; ++i) {
                std::uint16_t c = 0;
                if (!r.u16(p + 2 + 2 * i, c)) return false;
                s.push_back(static_cast<char>(c & 0xFF));
            }
            id = 0xFFFFFFFFu;
        } else {
            id = v; s.clear();
        }
        return true;
    };

    std::size_t first = 0; std::uint32_t n = 0;
    if (!entries(rsrcBase, first, n)) { error = "truncated resource root"; return false; }
    for (std::uint32_t i = 0; i < n; ++i) {
        std::size_t e = first + i * 8u;
        std::uint32_t id = 0; std::string s;
        if (!entryName(e, id, s)) { error = "bad type entry"; return false; }
        if (s != type) continue;

        std::uint32_t off = 0;
        if (!r.u32(e + 4, off) || !(off & kDirBit)) { error = "type is a leaf"; return false; }
        std::size_t nameDir = rsrcBase + (off & ~kDirBit);
        std::size_t nf = 0; std::uint32_t nn = 0;
        if (!entries(nameDir, nf, nn)) { error = "truncated name dir"; return false; }
        for (std::uint32_t j = 0; j < nn; ++j) {
            std::size_t ne = nf + j * 8u;
            std::uint32_t nid = 0; std::string ns;
            if (!entryName(ne, nid, ns)) { error = "bad name entry"; return false; }
            if (nid != name) continue;

            std::uint32_t noff = 0;
            if (!r.u32(ne + 4, noff) || !(noff & kDirBit)) { error = "name is a leaf"; return false; }
            std::size_t langDir = rsrcBase + (noff & ~kDirBit);
            std::size_t lf = 0; std::uint32_t ln = 0;
            if (!entries(langDir, lf, ln) || ln == 0) { error = "no language entry"; return false; }
            std::uint32_t doff = 0;
            if (!r.u32(lf + 4, doff)) { error = "bad language entry"; return false; }
            if (doff & kDirBit) { error = "language is a directory"; return false; }
            std::size_t de = rsrcBase + doff;
            std::uint32_t dataRva = 0, dataSize = 0;
            if (!r.u32(de, dataRva) || !r.u32(de + 4, dataSize)) {
                error = "truncated data entry"; return false;
            }
            std::size_t fo = 0;
            if (!toFile(dataRva, fo) || fo + dataSize > f.size()) {
                error = "resource data is outside the file"; return false;
            }
            out.assign(f.begin() + static_cast<long>(fo),
                       f.begin() + static_cast<long>(fo + dataSize));
            return true;
        }
        error = std::string("resource ") + type + " has no entry named " +
                std::to_string(name);
        return false;
    }
    error = std::string("no resource of type ") + type;
    return false;
}

std::uint8_t Image::deviceIndex(std::size_t i) const {
    // §10.3 invariant 4: block index and source offset come from ONE counter.
    // This is the only place +0x34 appears, so there is no second copy to drift.
    if (i >= blockCount())
        throw std::out_of_range("block index past end of image");
    return static_cast<std::uint8_t>(kBlockFirst + i);
}

bool Image::loadFromExecutable(const std::string& exePath, std::string& error) {
    bytes_.clear();
    checksum_ = 0;
    sha_.clear();

    std::vector<std::uint8_t> raw;
    if (!extractResource(exePath, kResourceType, kResourceName, raw, error))
        return false;

    // §4.2: any single preflight failure aborts. Each of these is a separate
    // reason to stop and each says which one it was.
    if (raw.size() != kExpectedImageSize) {
        error = "image is " + std::to_string(raw.size()) + " bytes, expected " +
                std::to_string(kExpectedImageSize);
        return false;
    }
    if (raw.size() % kBlockSize != 0) {
        error = "image size is not a whole number of 1024-byte blocks";
        return false;
    }
    if (raw.size() / kBlockSize != kExpectedBlockCount) {
        error = "image is " + std::to_string(raw.size() / kBlockSize) +
                " blocks, expected " + std::to_string(kExpectedBlockCount);
        return false;
    }
    std::string sha = sha256Hex(raw.data(), raw.size());
    if (sha != kExpectedSha256) {
        // The load-bearing one. §2 assumes the device validates nothing, so if
        // a wrong-but-well-formed image gets past here nothing downstream
        // catches it.
        error = "image SHA-256 is " + sha + "\n  expected " +
                std::string(kExpectedSha256) +
                "\n  This is NOT the firmware this build was derived against. "
                "Refusing.";
        return false;
    }
    bytes_ = std::move(raw);
    sha_ = sha;
    checksum_ = wholeImageChecksum(bytes_);
    return true;
}

bool Image::loadFromRelease(const std::string& exePath, const Release& rel,
                            std::string& error) {
    bytes_.clear();
    checksum_ = 0;
    sha_.clear();

    // ORDER MATTERS AND IS THE WHOLE SAFETY ARGUMENT. The .exe is hashed and
    // matched to the row BEFORE `rel.resourceId` is read, so the id can never
    // be influenced by the file's own contents. Reversing these two steps would
    // leave §1.4 saying the same words and meaning nothing.
    std::vector<std::uint8_t> exe;
    if (!readFile(exePath, exe, error)) return false;

    const std::string exeSha = sha256Hex(exe.data(), exe.size());
    if (exeSha != rel.updaterSha256) {
        error = "this file is not the " + std::string(rel.label) +
                " updater.\n  its SHA-256 is  " + exeSha +
                "\n  " + std::string(rel.label) + " is       " +
                std::string(rel.updaterSha256) +
                "\n  Refusing. An updater whose bytes are not in the manifest "
                "has no resource id, and guessing one is how a wrong-but-"
                "well-formed image reaches the device (CLAUDE.md \u00a72).";
        return false;
    }

    std::vector<std::uint8_t> raw;
    if (!extractResource(exePath, kResourceType, rel.resourceId, raw, error))
        return false;

    if (raw.size() != rel.imageSize) {
        error = "image is " + std::to_string(raw.size()) +
                " bytes, manifest says " + std::to_string(rel.imageSize);
        return false;
    }
    if (raw.size() % kBlockSize != 0) {
        error = "image size is not a whole number of 1024-byte blocks";
        return false;
    }
    if (raw.size() / kBlockSize != kExpectedBlockCount) {
        error = "image is " + std::to_string(raw.size() / kBlockSize) +
                " blocks, expected " + std::to_string(kExpectedBlockCount);
        return false;
    }
    const std::string sha = sha256Hex(raw.data(), raw.size());
    if (sha != rel.imageSha256) {
        error = "image SHA-256 is " + sha + "\n  manifest says " +
                std::string(rel.imageSha256) +
                "\n  The .exe matched the manifest but its FWFILE/" +
                std::to_string(rel.resourceId) + " did not. Refusing.";
        return false;
    }

    const std::uint32_t sum = wholeImageChecksum(raw);
    if (sum != rel.wholeImageChecksum) {
        // Belt and braces: the manifest records the value A0 03 will declare,
        // and the code computes it. If they disagree, one of them is wrong and
        // this is not the moment to find out which.
        char buf[128];
        std::snprintf(buf, sizeof buf,
                      "whole-image checksum is 0x%08x, manifest says 0x%08x",
                      sum, rel.wholeImageChecksum);
        error = buf;
        return false;
    }

    bytes_ = std::move(raw);
    sha_ = sha;
    checksum_ = sum;
    return true;
}

bool Image::loadFromBackup(const std::string& imagePath, std::string& error) {
    bytes_.clear();
    checksum_ = 0;
    sha_.clear();

    std::vector<std::uint8_t> raw;
    if (!readFile(imagePath, raw, error)) return false;

    // Same three structural checks the other two loaders make, in the same
    // order, for the same reason: a file of the wrong length is the sign that
    // it is not what the reader thinks it is.
    if (raw.size() != kExpectedImageSize) {
        error = imagePath + " is " + std::to_string(raw.size()) +
                " bytes, a backup is exactly " +
                std::to_string(kExpectedImageSize);
        return false;
    }
    if (raw.size() % kBlockSize != 0) {
        error = "image size is not a whole number of 1024-byte blocks";
        return false;
    }
    if (raw.size() / kBlockSize != kExpectedBlockCount) {
        error = "image is " + std::to_string(raw.size() / kBlockSize) +
                " blocks, expected " + std::to_string(kExpectedBlockCount);
        return false;
    }

    // The uniformity check checkBackupFile makes, repeated here rather than
    // called, because this is the WRITE side. A right-length buffer of one
    // repeated byte is what a failed A0 07 loop leaves behind, and writing that
    // back over a working application is the worst outcome this command has.
    bool varied = false;
    for (std::size_t i = 1; i < raw.size(); ++i)
        if (raw[i] != raw[0]) { varied = true; break; }
    if (!varied) {
        char b[64];
        std::snprintf(b, sizeof b, "0x%02x", raw[0]);
        error = imagePath + " is " + std::to_string(raw.size()) +
                " identical bytes (" + b + "); that is a failed read, not an "
                "image. Refusing to write it.";
        return false;
    }

    // THE PROVENANCE GATE. Everything above would pass for any 66560-byte file
    // with two different bytes in it; this is the clause that makes the command
    // "restore a backup" rather than "write an arbitrary file".
    const Provenance p = readProvenance(imagePath);
    if (!p.ok) {
        error = p.reason +
                ".\n  A restore writes to flash, so the image must be one THIS "
                "tool read off\n  THIS device. `read-firmware` writes " +
                provenancePathFor(imagePath) +
                " beside the\n  image it saves; without it there is nothing "
                "that says where these bytes\n  came from, and \u00a72 assumes "
                "the device validates nothing it is given.";
        return false;
    }

    const std::string sha = sha256Hex(raw.data(), raw.size());
    if (sha != p.sha256) {
        error = imagePath + " has changed since it was backed up.\n"
                "  it hashes to      " + sha + "\n"
                "  " + provenancePathFor(imagePath) + " records " + p.sha256 +
                "\n  Refusing. Take a fresh backup rather than trusting this "
                "one.";
        return false;
    }
    // The sidecar's own size field, checked against the file. Redundant with
    // the sha256 above and kept anyway: a sidecar that disagrees with itself is
    // a corrupt sidecar, and noticing that here costs nothing.
    if (p.size != 0 && p.size != raw.size()) {
        error = provenancePathFor(imagePath) + " records " +
                std::to_string(p.size) + " bytes but the image is " +
                std::to_string(raw.size()) + ". Refusing.";
        return false;
    }

    bytes_ = std::move(raw);
    sha_ = sha;
    checksum_ = wholeImageChecksum(bytes_);
    return true;
}

}  // namespace egg::fw
