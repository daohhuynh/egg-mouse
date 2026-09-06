#include "egg/ConfigRecord.h"

#include <string>

#include <cctype>
#include <cstdio>
#include <cstdlib>
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
    // BYTE 0 IS NOT VALIDATED, AND MUST NOT BE. It is the report-id slot, and
    // wire-observed.md §2.1 establishes the device never sends it: on Windows
    // the vendor's app writes 0xA1 there itself, and this device answers EVERY
    // feature read with 0xA1 regardless of which report was requested -- so
    // even the captured vendor reply has 0xA1, not 0xA0. On macOS hidapi
    // leaves it 0x00 (observed 2026-09-05, first read from the real device).
    //
    // This used to require r[0] == kReportLarge (0xA0), a value that appears
    // there on NO platform. It passed the suite only because MockConfigDevice
    // set the byte the check wanted. The status byte at kStatusOffset is the
    // real signal and Transport already checks it.
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

// ---------------------------------------------------------------------------
// Button mapping. Every byte below is [D] from cfg107; see config-protocol.md
// §7.17 for the derivation and the address of each handler.
// ---------------------------------------------------------------------------
namespace egg::cfg {

const ButtonAction kButtonActions[] = {
    // MOUSE -- +1 is the button mask.
    {"left-click",   "mouse", 0x00, 0x01, ButtonPayload::None, "cfg107 0x40774e"},
    {"right-click",  "mouse", 0x00, 0x02, ButtonPayload::None, "cfg107 0x4077f4"},
    {"middle-click", "mouse", 0x00, 0x04, ButtonPayload::None, "cfg107 0x40789a"},
    {"forward",      "mouse", 0x00, 0x10, ButtonPayload::None, "cfg107 0x407940"},
    {"back",         "mouse", 0x00, 0x08, ButtonPayload::None, "cfg107 0x4079e6"},
    // MOUSE -- scroll is a separate action type, +1 a signed step.
    {"scroll-up",    "mouse", 0x01, 0x01, ButtonPayload::None, "cfg107 0x407a8c"},
    {"scroll-down",  "mouse", 0x01, 0xFF, ButtonPayload::None, "cfg107 0x407b33"},
    // CPI.
    {"fixed-cpi",    "cpi",   0x0C, 0x00, ButtonPayload::FixedCpi, "cfg107 0x407c6f"},
    {"cpi-loop",     "cpi",   0x09, 0xF1, ButtonPayload::None, "cfg107 0x407ccf"},
    // MEDIA -- +1 is the low byte of a HID Consumer Page usage. The two AL
    // usages are 0x01xx and carry a different action type; that is the whole
    // reason they fit in one byte (§7.17).
    {"play-pause",   "media", 0x20, 0xCD, ButtonPayload::None, "cfg107 0x407d76"},
    {"next",         "media", 0x20, 0xB5, ButtonPayload::None, "cfg107 0x407e1d"},
    {"previous",     "media", 0x20, 0xB6, ButtonPayload::None, "cfg107 0x407ec4"},
    {"mute",         "media", 0x20, 0xE2, ButtonPayload::None, "cfg107 0x407f6b"},
    {"volume-up",    "media", 0x20, 0xE9, ButtonPayload::None, "cfg107 0x408012"},
    {"volume-down",  "media", 0x20, 0xEA, ButtonPayload::None, "cfg107 0x4080b9"},
    {"browser",      "media", 0x18, 0x96, ButtonPayload::None, "cfg107 0x408160"},
    {"explorer",     "media", 0x18, 0x94, ButtonPayload::None, "cfg107 0x40820a"},
    // KEYBOARD and DISABLE.
    {"key",          "keyboard", 0x02, 0x00, ButtonPayload::Key, "cfg107 0x408690"},
    {"disable",      "disable",  0xFF, 0x00, ButtonPayload::None, "cfg107 0x4082b4"},
};
const std::size_t kButtonActionCount =
    sizeof(kButtonActions) / sizeof(kButtonActions[0]);

const ButtonSlot kButtonSlots[] = {
    {"left",       0, false,
     "the vendor's Button Mapping page has no Left Button row (§7.15). Remapping "
     "left-click away leaves no way to click OK in any dialog, including ours"},
    {"right",      1, true,  nullptr},
    {"middle",     2, true,  nullptr},
    {"forward",    3, true,  nullptr},
    {"back",       4, true,  nullptr},
    {"cpi-button", 5, false,
     "entry 5, default 09 f1 = CPI LOOP, so it is the underside CPI button "
     "(§7.17). The vendor exposes no row for it and no capture has ever moved "
     "it, so how the firmware reacts to a change is untested"},
    {"wheel-up",   6, true,  nullptr},
    {"wheel-down", 7, true,  nullptr},
};
const std::size_t kButtonSlotCount =
    sizeof(kButtonSlots) / sizeof(kButtonSlots[0]);

const ButtonAction* findButtonAction(const char* name) {
    if (!name) return nullptr;
    for (std::size_t i = 0; i < kButtonActionCount; ++i)
        if (std::strcmp(kButtonActions[i].name, name) == 0) return &kButtonActions[i];
    return nullptr;
}

const ButtonSlot* findButtonSlot(const char* name) {
    if (!name) return nullptr;
    for (std::size_t i = 0; i < kButtonSlotCount; ++i)
        if (std::strcmp(kButtonSlots[i].name, name) == 0) return &kButtonSlots[i];
    return nullptr;
}

namespace {
// cfg107 0x40a120 translates a Windows virtual-key code to a HID usage. Four
// arithmetic ranges plus a jump table, and every value it produces is the
// standard HID Keyboard/Keypad Page usage -- checked for all 27 jump-table
// entries and all four ranges (§7.17). Reproduced here by HID usage directly,
// because we have no virtual-key codes to translate.
struct NamedKey { const char* name; std::uint8_t usage; };
const NamedKey kNamedKeys[] = {
    {"enter", 0x28}, {"escape", 0x29}, {"esc", 0x29}, {"backspace", 0x2A},
    {"tab", 0x2B}, {"space", 0x2C}, {"minus", 0x2D}, {"equal", 0x2E},
    {"leftbracket", 0x2F}, {"rightbracket", 0x30}, {"backslash", 0x31},
    {"semicolon", 0x33}, {"quote", 0x34}, {"grave", 0x35}, {"comma", 0x36},
    {"period", 0x37}, {"slash", 0x38}, {"capslock", 0x39},
    {"insert", 0x49}, {"home", 0x4A}, {"pageup", 0x4B}, {"delete", 0x4C},
    {"end", 0x4D}, {"pagedown", 0x4E},
    {"right", 0x4F}, {"left", 0x50}, {"down", 0x51}, {"up", 0x52},
};
}  // namespace

bool hidKeycode(const char* name, std::uint8_t& out) {
    if (!name || !*name) return false;
    std::string s(name);
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // a..z -> 0x04..0x1D. cfg107 0x40a130: VK-0x3d, and VK_A is 0x41.
    if (s.size() == 1 && s[0] >= 'a' && s[0] <= 'z') {
        out = static_cast<std::uint8_t>(0x04 + (s[0] - 'a'));
        return true;
    }
    // 1..9 -> 0x1E..0x26, 0 -> 0x27. cfg107 0x40a149 and 0x40a13a.
    if (s.size() == 1 && s[0] >= '1' && s[0] <= '9') {
        out = static_cast<std::uint8_t>(0x1E + (s[0] - '1'));
        return true;
    }
    if (s == "0") { out = 0x27; return true; }
    // f1..f12 -> 0x3A..0x45. cfg107 0x40a156: VK-0x36, VK_F1 is 0x70.
    if (s.size() >= 2 && s[0] == 'f') {
        char* end = nullptr;
        long n = std::strtol(s.c_str() + 1, &end, 10);
        if (end && !*end && n >= 1 && n <= 12) {
            out = static_cast<std::uint8_t>(0x3A + (n - 1));
            return true;
        }
    }
    // kp0..kp9. cfg107 0x40a163: VK-8 for 1..9, and 0x40a16d maps 0 to 0x62.
    if (s.size() == 3 && s[0] == 'k' && s[1] == 'p' && s[2] >= '0' && s[2] <= '9') {
        out = s[2] == '0' ? 0x62 : static_cast<std::uint8_t>(0x59 + (s[2] - '1'));
        return true;
    }
    for (const NamedKey& k : kNamedKeys)
        if (s == k.name) { out = k.usage; return true; }
    return false;
}

bool hidModifiers(const char* spec, std::uint8_t& out) {
    out = 0;
    if (!spec || !*spec) return true;
    std::string s(spec), tok;
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::size_t i = 0;
    while (i <= s.size()) {
        if (i == s.size() || s[i] == '+') {
            if (!tok.empty()) {
                // §7.12: the standard USB HID modifier byte, low nibble only.
                if      (tok == "ctrl"  || tok == "control") out |= 0x01;
                else if (tok == "shift")                     out |= 0x02;
                else if (tok == "alt")                       out |= 0x04;
                else if (tok == "win"   || tok == "gui")     out |= 0x08;
                else return false;
            }
            tok.clear();
        } else {
            tok += s[i];
        }
        ++i;
    }
    return true;
}

bool encodeButtonEntry(const ButtonAction& a, long arg, std::uint8_t mods,
                       std::uint8_t keepPlus6, std::uint8_t out[kButtonEntryLen],
                       const char** err) {
    const char* dummy = nullptr;
    if (!err) err = &dummy;
    *err = nullptr;
    for (std::size_t i = 0; i < kButtonEntryLen; ++i) out[i] = 0;
    out[0] = a.b0;
    out[1] = a.b1;
    // +6 is the multiclick filter and is NOT ours. It comes back from the read
    // and goes out unchanged; the vendor's handlers never write it either.
    out[6] = keepPlus6;

    switch (a.payload) {
    case ButtonPayload::None:
        if (mods) { *err = "this action takes no modifiers"; return false; }
        return true;
    case ButtonPayload::FixedCpi: {
        if (mods) { *err = "fixed-cpi takes no modifiers"; return false; }
        // The vendor writes X and Y as two 16-bit LE words and this path sets
        // them equal: the Button Mapping dialog has one CPI box, not two.
        if (arg < 50 || arg > 26000) {
            *err = "fixed-cpi wants a CPI value between 50 and 26000";
            return false;
        }
        const std::uint16_t v = static_cast<std::uint16_t>(arg);
        out[2] = static_cast<std::uint8_t>(v & 0xFF);
        out[3] = static_cast<std::uint8_t>(v >> 8);
        out[4] = out[2];
        out[5] = out[3];
        return true;
    }
    case ButtonPayload::Key:
        if (arg <= 0 || arg > 0xFF) { *err = "no such key"; return false; }
        if (mods & 0xF0) { *err = "only ctrl/shift/alt/win are offered"; return false; }
        out[1] = mods;                                   // §7.12 modifier byte
        out[2] = static_cast<std::uint8_t>(arg);         // HID keycode
        return true;
    }
    *err = "unknown payload kind";
    return false;
}

}  // namespace egg::cfg
