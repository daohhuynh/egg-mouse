#include "egg/ConfigRecord.h"

#include <string>

#include <cstdio>
#include <cstring>

namespace egg::cfg {

const char* describe(UnknownBytes p) {
    switch (p) {
        case UnknownBytes::Preserve:   return "preserve record 0x01-0x04 as read";
        case UnknownBytes::MatchVendor: return "zero record 0x01-0x04, as the vendor does";
    }
    return "?";
}

bool parseUnknownBytes(const char* s, UnknownBytes& out) {
    if (!s) return false;
    const std::string v(s);
    if (v == "preserve") { out = UnknownBytes::Preserve;   return true; }
    if (v == "vendor")   { out = UnknownBytes::MatchVendor; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// Structural validation
// ---------------------------------------------------------------------------
namespace {

// The shared body, so the logging and non-logging overloads cannot drift.
bool plausibleImpl(const std::vector<std::uint8_t>& r, Log* log) {
    if (r.size() != kLargeLen) {
        if (log) log->warn("record is not 1041 bytes");
        return false;
    }
    if (r[0] != kReportLarge) {
        if (log) log->warn("record does not begin with report id 0xA0");
        return false;
    }
    const std::uint8_t* p = r.data() + kPayloadOffset;
    bool allSame = true;
    for (std::size_t i = 1; i < kPayloadLen; ++i)
        if (p[i] != p[0]) { allSame = false; break; }
    if (allSame) {
        if (log) {
            char b[96];
            std::snprintf(b, sizeof b,
                "all 1024 payload bytes are 0x%02x -- that is not a settings record",
                p[0]);
            log->warn(b);
        }
        return false;
    }
    if (log) {
        unsigned distinct = 0; bool seen[256] = {};
        for (std::size_t i = 0; i < kPayloadLen; ++i)
            if (!seen[p[i]]) { seen[p[i]] = true; ++distinct; }
        char b[96];
        std::snprintf(b, sizeof b,
                      "%u distinct byte values across the 1024-byte payload", distinct);
        log->note(b);
    }
    return true;
}

}  // namespace

bool plausible(const std::vector<std::uint8_t>& r, Log& log) {
    return plausibleImpl(r, &log);
}

bool plausible(const std::vector<std::uint8_t>& r) {
    return plausibleImpl(r, nullptr);
}

// ---------------------------------------------------------------------------
// Diff
// ---------------------------------------------------------------------------
std::vector<ByteChange> diff(const std::vector<std::uint8_t>& before,
                             const std::vector<std::uint8_t>& after) {
    std::vector<ByteChange> out;
    // A partial diff of a malformed read reads like a small change when it is
    // really an unknown one, so refuse rather than truncate.
    if (before.size() != kLargeLen || after.size() != kLargeLen) return out;
    for (std::size_t i = 0; i < kPayloadLen; ++i) {
        const std::uint8_t b = before[kPayloadOffset + i];
        const std::uint8_t a = after[kPayloadOffset + i];
        if (b != a) out.push_back({i, b, a});
    }
    return out;
}

// ---------------------------------------------------------------------------
// Encoders. Moved verbatim from Sources/egg-config/main.cpp 2026-09-05; every
// citation is the one that was already there.
// ---------------------------------------------------------------------------

// config-protocol.md §7.5. Record 0x05 holds 8000/rate; the vendor's own reader
// at cfg107 0x413a79 drops anything that is not one of these seven powers of
// two to a no-op arm, so an unlisted value would be silently ignored rather
// than rejected -- which is exactly why this encoder refuses it here instead.
bool encodePolling(long hz, std::uint8_t& out) {
    if (hz <= 0 || 8000 % hz != 0) return false;
    const long div = 8000 / hz;
    if (div < 1 || div > 64 || (div & (div - 1)) != 0) return false;
    out = static_cast<std::uint8_t>(div);
    return true;
}

// config-protocol.md §7.6. Record 0x0e is the CPI stage count, combo index + 1.
bool encodeCpiLevels(long n, std::uint8_t& out) {
    if (n < 1 || n > 4) return false;
    out = static_cast<std::uint8_t>(n);
    return true;
}

// config-protocol.md §7.8/§7.9. Three checkboxes that reach a whole byte, each
// stored by a `setne` -- so the byte is 0 or 1 and nothing else.
bool encodeBool(long v, std::uint8_t& out) {
    if (v != 0 && v != 1) return false;
    out = static_cast<std::uint8_t>(v);
    return true;
}

// config-protocol.md §7.9. Record 0x70 is the Sensor Angle Tuning trackbar
// position, stored raw by `movb %al` at cfg107 0x411b25 -- so a negative angle
// is two's complement. The RANGE is not derived: the vendor reads TBM_GETPOS
// and never clamps, so the bound lives in a TBM_SETRANGE we have not read. This
// encoder therefore accepts the full signed byte and says so, rather than
// inventing a limit that would silently reject a legal angle.
bool encodeSensorAngle(long deg, std::uint8_t& out) {
    if (deg < -128 || deg > 127) return false;
    out = static_cast<std::uint8_t>(static_cast<signed char>(deg));
    return true;
}

// The LOD options the capture exercised were 0..10 inclusive, eleven of them,
// one APPLY each (windows-run/02-basic.pcapng lines 19-29). The dropdown has
// eleven entries and the twelfth slot in the log found nothing to select.
bool encodeLod(long v, std::uint8_t& out) {
    if (v < 0 || v > 10) return false;
    out = static_cast<std::uint8_t>(v);
    return true;
}

// The active CPI stage, 0-based. Observed 0,1,2,3 as the four unlabelled radio
// buttons were clicked top to bottom (windows-run/06-cpi-stage.pcapng).
bool encodeCpiStage(long v, std::uint8_t& out) {
    if (v < 0 || v > 3) return false;
    out = static_cast<std::uint8_t>(v);
    return true;
}

// CPI Downshift and Smoothing are dropdowns whose stored value is NOT the item
// index. The vendor remaps both, via a jump table and a ladder respectively,
// and the remaps are unrelated to each other. Item numbers here are 1-based to
// match what a user counts down the dropdown.
//
// Both confirmed against the capture on 2026-09-05, four and three writes
// respectively, each moving only its own bits of record 0x0b.
bool encodeDownshift(long item, std::uint8_t& out) {
    static const std::uint8_t kMap[] = {2, 3, 1, 0};   // items 1..4
    if (item < 1 || item > 4) return false;
    out = kMap[item - 1];
    return true;
}

bool encodeSmoothing(long item, std::uint8_t& out) {
    static const std::uint8_t kMap[] = {2, 0, 1};      // items 1..3
    if (item < 1 || item > 3) return false;
    out = kMap[item - 1];
    return true;
}

const Settable kSettable[] = {
    {"polling",    0x05, encodePolling,
     "125, 250, 500, 1000, 2000, 4000 or 8000 (Hz)",
     "config-protocol.md §7.5, cfg107 0x413a79 / 0x413a94-0x413b8f"},
    {"cpi-levels", 0x0e, encodeCpiLevels,
     "1, 2, 3 or 4",
     "config-protocol.md §7.6, cfg107 0x410c47 / 0x410c59 CB_GETCURSEL"},
    {"angle-snapping", 0x0a, encodeBool, "0 or 1",
     "config-protocol.md §7.8, cfg107 0x40ecb8 setne -> obj 0x28, serialised 0x4042f8"},
    {"motion-sync", 0x0c, encodeBool, "0 or 1",
     "config-protocol.md §7.9, cfg107 0x411ad1 setne -> obj 0x2a, serialised 0x404306"},
    {"force-max-fps", 0x71, encodeBool, "0 or 1",
     "config-protocol.md §7.9, cfg107 0x411b0b setne -> obj 0x2d, serialised 0x4045cf"},
    {"sensor-angle", 0x70, encodeSensorAngle, "-128 to 127 (degrees, two's complement)",
     "config-protocol.md §7.9, cfg107 0x411b19 TBM_GETPOS / 0x411b25 -> obj 0x2c"},
    // NAMED AFTER THE BYTE, NOT AFTER THE CHECKBOX, and that is deliberate.
    // The vendor's control is captioned "Disable LED on Lift-Off" and stores
    // the INVERSE of its tick (sete at cfg107 0x40ecd2), so a field of that
    // name taking 0/1 would mean the opposite of what half of all users would
    // assume. Here 1 = the underside DPI indicator stays lit when the mouse is
    // lifted, which is the vendor default; 0 = it goes out, which is the
    // vendor's box TICKED. Confirmed on the device by the owner, 2026-09-05.
    {"led-on-liftoff", 0x08, encodeBool,
     "0 or 1 -- 1 = DPI indicator stays lit when lifted (default); "
     "0 = it goes out, i.e. the vendor's \"Disable LED on Lift-Off\" TICKED",
     "config-protocol.md §7.8, cfg107 0x40ecd2 SETE -> obj 0x26, serialised 0x4042ea"},

    // Added 2026-09-05, once the capture scored them. Every one of these was
    // already derived and cited; what changed is that each was then seen to
    // move the predicted byte to the predicted value on the real device.
    {"lod", 0x09, encodeLod, "0 to 10 (eleven lift-off distance steps)",
     "notes/config-wire-observed.md §3; 11 writes, windows-run/02-basic lines 19-29"},
    {"cpi-stage", 0x0d, encodeCpiStage,
     "0 to 3 -- which CPI stage is ACTIVE, not how many exist (that is cpi-levels)",
     "notes/config-wire-observed.md §4; windows-run/06-cpi-stage, four radio "
     "buttons with APPLY never pressed"},
    {"slamclick-filter", 0x06, encodeBool, "0 or 1",
     "config-protocol.md §7.8, cfg107 0x40677a/0x406786; scored against "
     "windows-run/04-buttons line 1, which flipped bit 0 and held the rest",
     0x01, 0},
    {"cpi-downshift", 0x0b, encodeDownshift,
     "1 to 4, the dropdown item counting from the top (stored remapped: "
     "1->2, 2->3, 3->1, 4->0)",
     "config-protocol.md §7.9, cfg107 jump table 0x411bc0; scored against "
     "windows-run/03-sensor lines 8-11",
     0x0C, 2},
    {"smoothing", 0x0b, encodeSmoothing,
     "1 to 3, the dropdown item counting from the top (stored remapped: "
     "1->2, 2->0, 3->1)",
     "config-protocol.md §7.9, cfg107 ladder 0x411b36-0x411b4d; scored against "
     "windows-run/03-sensor lines 12-14",
     0x03, 0},
};
const std::size_t kSettableCount = sizeof(kSettable) / sizeof(kSettable[0]);

const Withheld kWithheld[] = {
    {"disable-led-on-liftoff",
     "the vendor's caption, and deliberately not our field name, because the "
     "vendor stores its INVERSE (sete at cfg107 0x40ecd2). Use `led-on-liftoff`, "
     "which is named after the byte: set it to 0 to get this checkbox's effect"},
    {"multiclick-filter",
     "record 0x3d + 7n, and it SHARES its byte with that button's SPDT mode: "
     "0..25 is a filter value, 0xf0 is GX Safe and 0xf1 is GX Speed. One field "
     "cannot express both, and picking the wrong encoding silently changes the "
     "switch mode. Observed but not yet given a safe interface"},
    {"button-mapping",
     "records 0x37..0x6e, eight seven-byte entries. Five action types observed "
     "(mouse mask, keyboard, media, fixed CPI, disable) but entries 5 and 7 were "
     "never exercised, so the block is not fully known (§1.2a)"},
};
const std::size_t kWithheldCount = sizeof(kWithheld) / sizeof(kWithheld[0]);

const Settable* findSettable(const char* name) {
    if (!name) return nullptr;
    for (std::size_t i = 0; i < kSettableCount; ++i)
        if (std::strcmp(name, kSettable[i].name) == 0) return &kSettable[i];
    return nullptr;
}

std::uint8_t composeByte(std::uint8_t old, const Settable& f, std::uint8_t value) {
    if (f.mask == 0xFF && f.shift == 0) return value;
    return static_cast<std::uint8_t>((old & ~f.mask) |
                                     ((value << f.shift) & f.mask));
}

}  // namespace egg::cfg
