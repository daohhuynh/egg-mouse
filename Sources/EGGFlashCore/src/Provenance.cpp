// Provenance.cpp -- see Provenance.h. Nothing here touches the device.
#include "egg/Provenance.h"

#include <unistd.h>
#include <stdlib.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

namespace egg::fw {
namespace {

constexpr const char* kProvenanceMagic = "egg-flash backup provenance v1";
constexpr const char* kReceiptMagic    = "egg-flash bootloader entry receipt v1";

std::string isoNow() {
    std::time_t t = std::time(nullptr);
    std::tm g{};
#if defined(_WIN32)
    gmtime_s(&g, &t);
#else
    gmtime_r(&t, &g);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &g);
    return buf;
}

// A tiny "key value" reader. Deliberately not a general parser: it accepts only
// the keys it is asked for and ignores everything else, so a longer file from a
// future version still reads rather than failing on an unknown line.
bool readKeyed(const std::string& path, std::string& magicOut,
               std::string (&vals)[8], const char* const (&keys)[8],
               std::string& error) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { error = "cannot open " + path; return false; }
    char line[512];
    bool first = true;
    while (std::fgets(line, sizeof line, f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (first) { magicOut = s; first = false; continue; }
        const std::size_t sp = s.find(' ');
        if (sp == std::string::npos) continue;
        const std::string k = s.substr(0, sp), v = s.substr(sp + 1);
        for (int i = 0; i < 8; ++i)
            if (keys[i] && k == keys[i]) vals[i] = v;
    }
    std::fclose(f);
    return true;
}

}  // namespace

std::string entryReceiptPath() {
    // HOME, not getcwd(). See the header: a Finder-launched .app runs with a
    // working directory of "/", so a cwd-relative receipt could never be
    // written from the GUI and every flash from the app would then refuse.
    if (const char* home = std::getenv("HOME"))
        if (*home) return std::string(home) + "/" + kEntryReceiptName;
    // No HOME at all. Fall back to the current directory rather than an
    // absolute path nobody owns; the callers print whatever this returns.
    char cwd[4096];
    if (!getcwd(cwd, sizeof cwd)) return kEntryReceiptName;
    return std::string(cwd) + "/" + kEntryReceiptName;
}

std::string provenancePathFor(const std::string& imagePath) {
    return imagePath + ".origin";
}

bool writeProvenance(const std::string& imagePath, const std::string& sha256,
                     std::size_t size, std::uint16_t bootloaderPid,
                     std::uint16_t bcdDevice, const std::string& product,
                     const std::string& entry, std::string& error) {
    const std::string path = provenancePathFor(imagePath);
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { error = "cannot write " + path; return false; }
    const int n = std::fprintf(f,
        "%s\n"
        "sha256 %s\n"
        "size %zu\n"
        "device-pid 0x%04x\n"
        "bcd-device 0x%04x\n"
        "product %s\n"
        "entry %s\n"
        "taken %s\n"
        "\n"
        "# Written by `egg-flash read-firmware`. `restore-firmware` refuses an\n"
        "# image with no such file, or whose bytes no longer hash to the sha256\n"
        "# above. Keep it next to the image. Deleting it does not corrupt the\n"
        "# backup -- it makes it unrestorable by this tool, which is the point.\n",
        kProvenanceMagic, sha256.c_str(), size, bootloaderPid, bcdDevice,
        product.c_str(), entry.c_str(), isoNow().c_str());
    const bool closed = std::fclose(f) == 0;
    if (n <= 0 || !closed) { error = "short write to " + path; return false; }
    return true;
}

Provenance readProvenance(const std::string& imagePath) {
    Provenance p;
    const std::string path = provenancePathFor(imagePath);
    const char* const keys[8] = {"sha256", "size", "bcd-device", "product",
                                 "taken", "entry", nullptr, nullptr};
    std::string vals[8], magic, err;
    if (!readKeyed(path, magic, vals, keys, err)) {
        p.reason = "no provenance file at " + path;
        return p;
    }
    if (magic != kProvenanceMagic) {
        p.reason = path + " is not an egg-flash provenance file";
        return p;
    }
    if (vals[0].empty()) { p.reason = path + " records no sha256"; return p; }
    p.sha256 = vals[0];
    p.size = vals[1].empty() ? 0 : std::strtoul(vals[1].c_str(), nullptr, 10);
    p.bcdDevice = static_cast<std::uint16_t>(
        vals[2].empty() ? 0 : std::strtoul(vals[2].c_str(), nullptr, 0));
    p.product = vals[3];
    p.taken = vals[4];
    p.entry = vals[5].empty() ? "unknown" : vals[5];
    p.ok = true;
    return p;
}

bool writeEntryReceipt(std::uint16_t bcdDevice, const std::string& product,
                       std::string& error) {
    const std::string path = entryReceiptPath();
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { error = "cannot write " + entryReceiptPath(); return false; }
    const int n = std::fprintf(f,
        "%s\n"
        "bcd-device 0x%04x\n"
        "product %s\n"
        "sent %s\n"
        "\n"
        "# `enter-bootloader` wrote this after A1 3A went out AND the mouse came\n"
        "# back with the identity above. `flash` and `restore-firmware` require\n"
        "# it when they find the mouse ALREADY in the bootloader, because they\n"
        "# cannot otherwise tell an A1 3A entry from a LEFT+RIGHT button entry --\n"
        "# the PID, bcdDevice and product string are identical either way, and\n"
        "# engineering-rules.md 4.2b says firmware work enters by A1 3A.\n"
        "#\n"
        "# It is deleted after a completed flash, because a completed flash is\n"
        "# what clears the A1 3A latch.\n"
        "#\n"
        "# What it does NOT prove: entering by A1 3A, unplugging, then\n"
        "# button-entering leaves this file in place. That is a deliberate\n"
        "# sequence rather than an accident, the same standard 4.2b already\n"
        "# accepts for a backup going stale.\n",
        kReceiptMagic, bcdDevice, product.c_str(), isoNow().c_str());
    const bool closed = std::fclose(f) == 0;
    if (n <= 0 || !closed) {
        error = "short write to " + entryReceiptPath();
        return false;
    }
    return true;
}

EntryReceipt readEntryReceipt() {
    EntryReceipt r;
    const char* const keys[8] = {"bcd-device", "product", "sent",
                                 nullptr, nullptr, nullptr, nullptr, nullptr};
    std::string vals[8], magic, err;
    if (!readKeyed(entryReceiptPath(), magic, vals, keys, err)) {
        r.reason = "no " + entryReceiptPath();
        return r;
    }
    if (magic != kReceiptMagic) {
        r.reason = entryReceiptPath() + " is not an egg-flash receipt";
        return r;
    }
    r.bcdDevice = static_cast<std::uint16_t>(
        vals[0].empty() ? 0 : std::strtoul(vals[0].c_str(), nullptr, 0));
    r.product = vals[1];
    r.sent = vals[2];
    r.present = true;
    return r;
}

void clearEntryReceipt() { std::remove(entryReceiptPath().c_str()); }

bool sameFile(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    if (a == b) return true;
    char ra[4096], rb[4096];
    const char* pa = realpath(a.c_str(), ra);
    const char* pb = realpath(b.c_str(), rb);
    // If EITHER cannot be resolved there is nothing to compare beyond the
    // strings, which already differ. Returning false here is the safe
    // direction: it lets the run continue to the checks that open the files.
    if (!pa || !pb) return false;
    return std::string(pa) == std::string(pb);
}

EntryGate entryGate(bool allowButtonEntry, std::uint16_t liveBcd,
                    const std::string& liveProduct) {
    EntryGate g;
    g.receipt = readEntryReceipt();
    if (g.receipt.present &&
        (g.receipt.bcdDevice != liveBcd || g.receipt.product != liveProduct)) {
        // Present but about something else. Deliberately NOT treated as absent:
        // "there is no receipt" and "there is a receipt for a different device"
        // send a person to different places, and the second is the one worth
        // stopping to think about.
        char b[64];
        std::snprintf(b, sizeof b, "0x%04x", g.receipt.bcdDevice);
        g.receipt.present = false;
        g.reason = entryReceiptPath() + " describes a different "
                   "bootloader (" + b + " \"" + g.receipt.product +
                   "\") than the one attached";
        if (allowButtonEntry) { g.allowed = true; g.overridden = true; }
        return g;
    }
    if (g.receipt.present) {
        g.allowed = true;
        return g;
    }
    // The escape hatch. the owner chose "enforce it, with an escape hatch"
    // (2026-09-06); the hatch is a flag that has to be typed in full and is
    // deliberately not --yes, because --yes is typed every time (\u00a74.2c).
    if (allowButtonEntry) {
        g.allowed = true;
        g.overridden = true;
        return g;
    }
    g.reason = g.receipt.reason;
    return g;
}

}  // namespace egg::fw
