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

// config-protocol.md §7.9 and §7.16. Record 0x70 <- object 0x2c is the Sensor
// Angle Tuning position, stored as a signed byte.
//
// NARROWED 2026-09-06 from -128..127 to -127..127, by the citation audit.
//
// The old bound was reasoned as follows: the value is stored raw by `movb` at
// cfg107 0x411b25, the vendor "never clamps", the real bound must live in a
// TBM_SETRANGE we have not read, and so the honest thing is to accept the whole
// signed byte rather than invent a limit. Every step of that is wrong except
// the first. The vendor DOES clamp, twenty lines further on:
//
//   0x411f62  83 f8 81               cmpl $-0x7f, %eax        SIGNED compare
//   0x411f65  7d 0a                  jge  0x411f71            jge, not jae
//   0x411f67  c7 86 74 03 .. 81ffffff movl $0xffffff81, 0x374(%esi)   -> -127
//   0x411f71  b8 7f 00 00 00         movl $0x7f, %eax
//   0x411f7c  7e 06                  jle  0x411f84            jle, not jbe
//   0x411f7e  89 86 74 03 00 00      movl %eax, 0x374(%esi)          -> +127
//   0x411f84  8a 96 74 03 00 00      movb 0x374(%esi), %dl    truncate
//   0x411f8a  88 15 3c f2 57 00      movb %dl, 0x57f23c       -> object 0x2c
//
// A signed clamp to a symmetric +/-127 followed by a byte truncation. The
// SIGNED jump forms are the load-bearing detail: an unsigned field would use
// jae/jbe and could not clamp at a negative bound at all.
//
// So 0x80 (-128) is a value the vendor's own UI cannot produce. Emitting it
// would be writing a byte whose meaning is [G] to a mouse there is only one of,
// which §1.3 forbids. Accepting it was not caution; it was a guess wearing
// caution's clothes -- and the comment that justified it asserted an absence
// ("never clamps") that no search had established, which is exactly what §1.2a
// exists to catch.
bool encodeSensorAngle(long deg, std::uint8_t& out) {
    if (deg < -127 || deg > 127) return false;
    out = static_cast<std::uint8_t>(static_cast<signed char>(deg));
    return true;
}

// config-protocol.md §7.18. Record 0x09 <- object 0x27. Was the only writable
// field with no address citation at all; its range came from counting writes in
// a capture and reading a log that is now quarantined (CLAUDE.md §1.1a). It is
// now [D], and the derivation is better than the count was:
//
//   cfg107 0x40ec62  cmpl $0xa, %eax ; ja 0x40ec9e   bound check, 11 items
//   cfg107 0x40ec67  jmpl *0x40ee54(,%eax,4)         11-entry jump table
//   cfg107 0x40ec6e..0x40ec9e                        eleven `movb $imm, 0x27(%edi)`
//                                                    with imm exactly 0x00..0x0a
//
// The eleven arms are in jump-table order, not numeric order, which is why a
// scan for a contiguous run finds nothing; the SET is what matters and it is
// exactly {0..10}. [O] agrees: record 0x09 takes 00..0a and nothing else across
// every settings frame in windows-run, default 0x03.
//
// TWO THINGS THIS BYTE DOES THAT THE OLD COMMENT DID NOT KNOW, both found by
// auditing the citation rather than by looking for them.
//
// 1. THE LIST LENGTH IS CONDITIONAL. cfg107 0x40ec42 tests `cmpb $0x1,
//    0x57f23b` -- object 0x2b, which recmap maps to record 0x6f -- and when it
//    is 1 the dropdown collapses to TWO items storing only 1 and 2. This device
//    reads 0x00 there in every capture, before and after a factory reset and
//    after a firmware flash, so the eleven-value branch is the live one. If a
//    future device or firmware reports 0x01 at record 0x6f, the values 0 and
//    3..10 become unreachable through the vendor's own UI and writing one would
//    be [G]. `set` refuses nothing here today, deliberately -- see §7.18 -- but
//    that is a live caveat, not a settled question.
// 2. A SECOND ENCODING WRITES THE SAME BYTE. cfg107 0x40fa95-0x40fb0d is
//    another CB_GETCURSEL (0x147) mapping eleven indices to 0xc2, 0xc4, 0xc6,
//    0xc9, 0xca, 0xcc, 0xcd, 0xd0, 0xd4, 0xd7, 0xd9 into the same object 0x27.
//    Not one of those values has ever appeared on this device. That does NOT
//    make it dead code (§1.2a: absence of observation is not absence), and the
//    two ranges are disjoint, so a byte in 0xc2..0xd9 is a positive signal that
//    something other than the eleven-item list wrote it. We never emit one.
bool encodeLod(long v, std::uint8_t& out) {
    if (v < 0 || v > 10) return false;
    out = static_cast<std::uint8_t>(v);
    return true;
}

// config-protocol.md §7.18. Record 0x0d <- object 0x0a. Also had no address
// citation; also now [D]. cfg107 0x40edf0-0x40ee4c is four BM_GETCHECK calls
// (message 0xf0) on dialog-135 controls 0x13f4, 0x1468, 0x14dc and 0x1550,
// each storing a literal 0, 1, 2 or 3 to 0xa(%edi):
//
//   0x40edfd  movb $0x0, 0xa(%edi)      control 0x13f4
//   0x40ee17  movb $0x1, 0xa(%edi)      control 0x1468
//   0x40ee31  movb $0x2, 0xa(%edi)      control 0x14dc
//   0x40ee4c  movb $0x3, 0xa(%edi)      control 0x1550
//
// Four mutually exclusive checkboxes storing an index is a radio group, so the
// range is 0..3 and the value is the ACTIVE CPI STAGE. The old comment said the
// same thing sourced to "the four unlabelled radio buttons clicked top to
// bottom", which was a label read out of the quarantined log; the ordering is
// now the vendor's own control ids instead.
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
    {"sensor-angle", 0x70, encodeSensorAngle, "-127 to 127 (degrees, two's complement)",
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
    // The scale is NOT arbitrary and is worth naming in the help text: cfg107
    // keeps eleven `.rdata` millimetre strings, "0.7mm" through "1.7mm" in
    // 0.1mm steps, and the index into them IS the stored value (§7.25). A user
    // told "0 to 10" has to guess which end is closer to the pad.
    {"lod", 0x09, encodeLod,
     "0 to 10 -- lift-off distance, 0 = 0.7mm up to 10 = 1.7mm in 0.1mm steps",
     "config-protocol.md §7.18, cfg107 0x40ec62 bound / 0x40ec6e-0x40ec9e "
     "eleven stores of 0x00-0x0a; scored against windows-run/02-basic"},
    {"cpi-stage", 0x0d, encodeCpiStage,
     "0 to 3 -- which CPI stage is ACTIVE, not how many exist (that is cpi-levels)",
     "config-protocol.md §7.18, cfg107 0x40edf0-0x40ee4c four BM_GETCHECK "
     "storing 0/1/2/3; scored against windows-run/06-cpi-stage"},
    {"slamclick-filter", 0x06, encodeBool, "0 or 1",
     "config-protocol.md §7.8, cfg107 0x40677a/0x406786; scored against "
     "windows-run/04-buttons line 1, which flipped bit 0 and held the rest",
     0x01, 0},
    // Same byte as slamclick-filter, bit 4, and derived to the same standard:
    // written twice, by the click handler at 0x411e7a/0x411e86 and by the APPLY
    // collector at 0x411aeb/0x411af1, both agreeing on the bit. The `accepts`
    // string carries the caveat rather than hiding it -- CLAUDE.md §1.2a's rule
    // that absence is a claim cuts both ways, and silently withholding a fully
    // derived field tells the user less than offering it with the warning.
    {"motion-jitter-filter", 0x06, encodeBool,
     "0 or 1 -- NOTE: the vendor's own Advanced Sensor page HIDES this control "
     "on this model (prediction-scores.md #21, observed 2026-09-05), so nobody "
     "has ever set it and its effect on the sensor is unobserved. The BIT is "
     "derived; the BEHAVIOUR is not",
     "config-protocol.md §7.8, cfg107 0x411e7a/0x411e86 (click) and "
     "0x411aeb/0x411af1 (APPLY collect), both orb $0x10 / andb $-0x11",
     0x10, 4},
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
     "switch mode -- so `set` will not take it. Use `egg-config multiclick`, "
     "which names the mode and refuses GX on the three buttons that have no "
     "SPDT switch behind them (\u00a77.22.3)"},
    {"glass-mode",
     "record 0x6f (\u00a77.25). Derived, and deliberately not offered: it is the "
     "`Sensor Glass Mode` checkbox, control 1031, which cfg100/101/104 write "
     "(cfg104 0x411824 sets it, 0x411845 clears it) and cfg107 only reads. Four "
     "reasons to wait rather than four reasons it is wrong -- no capture has "
     "ever seen it written, what it does inside the sensor is [G], setting it "
     "silently changes what `lod` means, and Endgame withdrew the control by "
     "two separate mechanisms. `factory-reset` and `restore` both clear it"},
    {"multiclick-ack",
     "record 0x72 (\u00a77.23). [D] from cfg107 0x4045d9, and deliberately not "
     "offered: it records that someone ticked a warning checkbox in Endgame's "
     "Windows application. It configures no mouse behaviour, so listing it "
     "beside `polling` and `lod` would imply it does. Read-modify-write carries "
     "it through untouched"},
    {"button-mapping",
     "not withheld any more -- use `egg-config map`. Kept in this list only to "
     "say so, because the reason it WAS withheld is worth not forgetting: five "
     "action types had been observed on the wire and the rest were [G]. What "
     "changed is that all 19 became [D] from cfg107 (§7.17), not that the "
     "capture covered more"},
};
const std::size_t kWithheldCount = sizeof(kWithheld) / sizeof(kWithheld[0]);

// §7.25. One row per gate: the field, the byte that governs it, the only value
// of that byte under which the field's documented meaning holds, and why.
namespace {
struct Gate {
    const char*  field;
    std::size_t  governedBy;      // record offset of the governing byte
    std::uint8_t onlyWhen;        // its value under which `field` is as documented
    const char*  why;
};
const Gate kGates[] = {
    {"lod", 0x6f, 0x00,
     "SENSOR GLASS MODE is on (record 0x6f is not 0), and it decides what `lod` "
     "means. Endgame's own tool (cfg107 0x40ec42/0x40eeb6/0x40f1e5) offers "
     "eleven steps 0.7mm-1.7mm when this byte is 0 and only two, on a different "
     "scale, when it is 1 -- where 1 is 1.0mm rather than 0.8mm. Turn Sensor "
     "Glass Mode off first, with `factory-reset` or by restoring a saved "
     "record. Refusing costs no frame; guessing costs a wrong lift-off "
     "distance. See config-protocol.md \u00a77.25"},
};
}  // namespace

const std::size_t* gatedRecordOffsets(std::size_t& count) {
    static std::size_t offs[sizeof(kGates) / sizeof(kGates[0])];
    count = sizeof(kGates) / sizeof(kGates[0]);
    for (std::size_t i = 0; i < count; ++i) offs[i] = kGates[i].governedBy;
    return offs;
}

const char* capabilityRefusal(const Settable& f, const std::uint8_t* record) {
    if (!record) return nullptr;
    for (const Gate& g : kGates) {
        if (std::strcmp(f.name, g.field) != 0) continue;
        if (record[kPayloadOffset + g.governedBy] != g.onlyWhen) return g.why;
    }
    return nullptr;
}

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
    // 0x40865a sets the action type (`movb $0x2, %cl`) and 0x408690 stores it
    // plus the modifier byte assembled by the orb chain at 0x40866f-0x408689.
    // The type immediate is invisible to a linear disassembly here -- objdump
    // desynchronises and renders 0x40865a inside a bogus instruction -- which
    // is why Tests/test_citations.py works from raw bytes (§1.2b).
    {"key",          "keyboard", 0x02, 0x00, ButtonPayload::Key,
                                 "cfg107 0x40865a/0x408690"},
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
    // Added 2026-09-06 (§7.32). These 21 usages are every remaining target of
    // cfg107's VK->HID jump table at 0x40a185/0x40a18c, and without them
    // `map key:` could not express what the vendor's own screenshot shows in
    // its KEYBOARD KEY box -- "Left Shift", HID 0xE1.
    {"printscreen", 0x46}, {"prtsc", 0x46},
    {"scrolllock", 0x47}, {"pause", 0x48}, {"break", 0x48},
    {"numlock", 0x53},
    {"kp-slash", 0x54}, {"kp-star", 0x55}, {"kp-minus", 0x56},
    {"kp-plus", 0x57}, {"kp-dot", 0x63},
    {"menu", 0x65}, {"application", 0x65},
    // The KEYBOARD page's own volume usages, NOT the MEDIA menu's `volume-up`
    // (which is a different action type entirely, +0 = 0x20).
    {"kb-volume-up", 0x80}, {"kb-volume-down", 0x81},
    // A modifier pressed AS A KEY. Distinct from `key:ctrl+a`, where ctrl is
    // the +1 modifier byte and `a` is the usage: here the modifier IS the
    // usage and +1 stays 0. Both are reachable in the vendor's dialog.
    {"leftctrl", 0xE0},  {"leftshift", 0xE1},  {"leftalt", 0xE2},  {"leftwin", 0xE3},
    {"rightctrl", 0xE4}, {"rightshift", 0xE5}, {"rightalt", 0xE6}, {"rightwin", 0xE7},
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

// ---------------------------------------------------------------------------
// CPI stages -- config-protocol.md §7.19
// ---------------------------------------------------------------------------
// Transcribed from cfg107 `0x0040d880`-`0x0040d93a`, the ONE function every
// typed CPI passes through in the vendor tool. The magic multiplies there are
// the compiler's division-by-constant, not part of the protocol:
//
//   40d8ac  cmpl $0xa,%ecx      / jge 40d8ee      ; below 10?
//   40d8b1  movl $0xa,%ecx                        ; ...then treat it as 10
//   40d8b6  edx = ecx/10 (0xcccccccd, >>3); rem = ecx - edx*10
//   40d8c7  cmpl $0x5,%ecx / jb ; else incl %edx  ; ROUND HALF UP
//   40d8cd  edx *= 10
//   40d8d2  movl %edx,(%esi)                      ; the stored value
//   40d8ee  cmpl $0x7530,%ecx   / jle             ; 30000 ceiling
//   40d8f6  movl $0x7530,%ecx
//   40d8fd  cmpl $0xa,%ecx      / jae             ; 10 floor
//   40d918  cmpl $0x2710,%ecx   / jbe 40d8b6      ; <=10000 -> step 10
//   40d920  edx = ecx/50 (0x51eb851f, >>4); rem = ecx - edx*50
//   40d931  cmpl $0x19,%ecx / jb ; else incl %edx ; ROUND HALF UP at 25
//   40d937  edx *= 50
//
// The trackbar path reaches the same set independently: position 1..1400 with
// pos<=1000 -> pos*10 and pos>1000 -> (pos-800)*50 (`0x0040c2f0`-`0x0040c312`,
// X; `0x0040c354`-`0x0040c395`, Y). 1400 -> 30000, and the two step sizes and
// the changeover at 10000 agree exactly. That agreement is why this is [D]
// rather than one reading of one function.
long normaliseCpi(long v) {
    if (v > kCpiMax) v = kCpiMax;
    if (v < kCpiMin) v = kCpiMin;
    const long step = (v <= kCpiFineLimit) ? 10 : 50;
    const long q = v / step, rem = v - q * step;
    return (rem * 2 >= step ? q + 1 : q) * step;
}

bool encodeCpiStageEntry(long x, long y, std::uint8_t out[kCpiEntryLen],
                         const char** err) {
    // REFUSE rather than round. The vendor's edit box rounds silently because a
    // human is watching the number change; we are writing to a device with one
    // verify pass and a diff the user reads afterwards, and a value they did
    // not ask for would sail through both looking correct.
    for (long v : {x, y}) {
        if (v != normaliseCpi(v)) {
            if (err) *err = "not a CPI the vendor tool can produce";
            return false;
        }
    }
    // flag = X != Y, computed from THIS stage. cfg107 gets stage 4 wrong --
    // `0x0040edde`/`0x0040ede4` load stage 3's controls for stage 4's flag
    // (§7.8) -- and we deliberately do not reproduce that. §4.2 says mirror the
    // vendor's VERIFICATION; it does not say mirror its arithmetic, and the
    // flag's meaning is [D] while the bug is just a bug.
    out[0] = static_cast<std::uint8_t>(x != y ? 1 : 0);
    out[1] = static_cast<std::uint8_t>(x & 0xFF);
    out[2] = static_cast<std::uint8_t>((x >> 8) & 0xFF);
    out[3] = static_cast<std::uint8_t>(y & 0xFF);
    out[4] = static_cast<std::uint8_t>((y >> 8) & 0xFF);
    return true;
}

CpiStage decodeCpiStageEntry(const std::uint8_t* e) {
    CpiStage s;
    s.flag = e[0] != 0;
    s.x = static_cast<long>(e[1]) | (static_cast<long>(e[2]) << 8);
    s.y = static_cast<long>(e[3]) | (static_cast<long>(e[4]) << 8);
    return s;
}

// ---------------------------------------------------------------------------
// Left-handed mode -- config-protocol.md §7.20
// ---------------------------------------------------------------------------
// cfg107 `0x408b00` hard-sets whichever entry is the primary click to
// `00 01 00 00 00 00` and moves the user's assignment to the other. Byte `+6`
// is the multiclick filter (§7.22) and is NOT part of the action, which is why
// the state test below ignores it -- the vendor's arm B swaps `+6` and its arm
// A only copies it, so `+6` cannot be used to identify the state.
static const std::uint8_t kLeftClickEntry[6] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x00};

static bool isLeftClickDefault(const std::uint8_t* entry) {
    return std::memcmp(entry, kLeftClickEntry, sizeof kLeftClickEntry) == 0;
}

Handedness readHandedness(const std::uint8_t* record) {
    const std::uint8_t* e0 = record + kButtonBlockFirst;
    const std::uint8_t* e1 = e0 + kButtonEntryLen;
    const bool a = isLeftClickDefault(e0), b = isLeftClickDefault(e1);
    if (a == b) return Handedness::Unknown;   // neither, or ambiguously both
    return a ? Handedness::Right : Handedness::Left;
}

bool applyHandedness(const std::uint8_t* record, Handedness want,
                     std::uint8_t out[2 * kButtonEntryLen], bool& changed,
                     const char** err) {
    changed = false;
    if (want == Handedness::Unknown) {
        if (err) *err = "handedness must be `left` or `right`";
        return false;
    }
    const Handedness now = readHandedness(record);
    if (now == Handedness::Unknown) {
        if (err) *err = "the record is in neither handedness state -- refusing "
                        "rather than guessing which entry holds your mapping";
        return false;
    }
    const std::uint8_t* e0 = record + kButtonBlockFirst;
    const std::uint8_t* e1 = e0 + kButtonEntryLen;
    std::memcpy(out, e0, kButtonEntryLen);
    std::memcpy(out + kButtonEntryLen, e1, kButtonEntryLen);
    if (now == want) return true;             // nothing to write

    // The MOVE. `keep` is the entry holding the user's assignment; it becomes
    // the other one, and the vacated slot becomes left-click. Byte +6 is
    // swapped, which is cfg107's arm B; arm A copies it one way instead. We
    // swap in both directions ON PURPOSE -- copying would silently overwrite
    // one button's multiclick filter with the other's, and §7.22 makes that a
    // separate setting the user configured deliberately.
    const std::uint8_t* keep = (now == Handedness::Right) ? e1 : e0;
    std::uint8_t* dst = out + ((want == Handedness::Right) ? kButtonEntryLen : 0);
    std::uint8_t* vac = out + ((want == Handedness::Right) ? 0 : kButtonEntryLen);
    const std::uint8_t keepPlus6 = keep[6];
    const std::uint8_t vacPlus6  = (keep == e0 ? e1[6] : e0[6]);
    std::memcpy(dst, keep, kButtonEntryLen);
    std::memcpy(vac, kLeftClickEntry, sizeof kLeftClickEntry);
    dst[6] = keepPlus6;
    vac[6] = vacPlus6;
    changed = true;
    return true;
}

// ---------------------------------------------------------------------------
// Multiclick filter and SPDT mode -- config-protocol.md §7.22
// ---------------------------------------------------------------------------
bool encodeMulticlick(std::size_t button, const char* mode, long value,
                      std::uint8_t& out, const char** err) {
    if (button >= kMulticlickCount) {
        if (err) *err = "only left, right, middle, forward and back have a "
                        "multiclick filter";
        return false;
    }
    const bool hasSpdt = (button < 2);        // left and right only (§7.22.3)
    if (mode && std::strcmp(mode, "gx-speed") == 0) {
        if (!hasSpdt) {
            if (err) *err = "only the left and right buttons have an SPDT combo; "
                            "the vendor's page offers no GX mode for this one";
            return false;
        }
        out = kSpdtGxSpeed;
        return true;
    }
    if (mode && std::strcmp(mode, "gx-safe") == 0) {
        if (!hasSpdt) {
            if (err) *err = "only the left and right buttons have an SPDT combo; "
                            "the vendor's page offers no GX mode for this one";
            return false;
        }
        out = kSpdtGxSafe;
        return true;
    }
    if (!mode || std::strcmp(mode, "off") != 0) {
        if (err) *err = "mode must be `off`, `gx-speed` or `gx-safe`";
        return false;
    }
    if (value < 0 || value > kMulticlickMax) {
        if (err) *err = "the filter is 0 to 25 (CSliderCtrl::SetRange(0, 0x19) "
                        "at cfg107 0x405f84)";
        return false;
    }
    out = static_cast<std::uint8_t>(value);
    return true;
}

const char* describeMulticlick(std::uint8_t byte, long& value) {
    value = byte;
    if (byte == kSpdtGxSpeed) return "gx-speed";
    if (byte == kSpdtGxSafe)  return "gx-safe";
    if (byte <= kMulticlickMax) return "off";
    return nullptr;             // a value the vendor's page cannot produce
}

// Mirrors cfg107 `0x00401e70` exactly: clamp, then round to the nearest 10 with
// ties going up. See kFixedCpiMin/Max/Step in the header for the instruction
// addresses of each of those three steps.
long normaliseFixedCpi(long v) {
    if (v > kFixedCpiMax) v = kFixedCpiMax;
    if (v < kFixedCpiMin) v = kFixedCpiMin;
    const long q = v / kFixedCpiStep, rem = v - q * kFixedCpiStep;
    return (rem * 2 >= kFixedCpiStep ? q + 1 : q) * kFixedCpiStep;
}

bool encodeButtonEntry(const ButtonAction& a, long arg, long argY, std::uint8_t mods,
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
        // REFUSE rather than round, for the reason encodeCpiStageEntry gives:
        // the vendor's edit box rounds while a human watches the number change,
        // and we are writing to a device where a value the user did not ask for
        // would pass both the verify and the diff looking correct.
        for (long v : {arg, argY}) {
            if (v != normaliseFixedCpi(v)) {
                *err = "fixed-cpi wants 10 to 30000 in steps of 10 "
                       "(cfg107 0x401e70)";
                return false;
            }
        }
        // X to +2, Y to +4, independently -- cfg107 0x407c7e / 0x407ca0.
        const std::uint16_t x = static_cast<std::uint16_t>(arg);
        const std::uint16_t y = static_cast<std::uint16_t>(argY);
        out[2] = static_cast<std::uint8_t>(x & 0xFF);
        out[3] = static_cast<std::uint8_t>(x >> 8);
        out[4] = static_cast<std::uint8_t>(y & 0xFF);
        out[5] = static_cast<std::uint8_t>(y >> 8);
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
