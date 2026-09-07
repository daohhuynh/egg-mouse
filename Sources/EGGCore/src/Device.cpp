#include "egg/Device.h"

#include <hidapi.h>
#ifdef __APPLE__
#include <hidapi_darwin.h>
#endif

#include <cstdio>
#include <cwchar>

namespace egg {
namespace {

std::string narrow(const wchar_t* w) {
    if (!w) return {};
    std::string s;
    for (; *w; ++w) s += (*w < 0x80) ? static_cast<char>(*w) : '?';
    return s;
}

bool g_inited = false;

}  // namespace

bool initHid() {
    if (!g_inited) g_inited = (hid_init() == 0);
#ifdef __APPLE__
    // MUST come after hid_init: the header says calling it before hid_init or
    // after hid_exit has no effect, and hid_init resets it to 1.
    //
    // hidapi opens every device with kIOHIDOptionsTypeSeizeDevice on macOS,
    // which SEIZES it from the system. For a device the OS treats as an input
    // device that is refused with 0xE00002C1 kIOReturnNotPrivileged -- observed
    // on this mouse 2026-09-05, after Input Monitoring had already been granted
    // and the earlier 0xE00002E2 kIOReturnNotPermitted had gone away.
    //
    // Non-exclusive is not a workaround for that error, it is what the vendor
    // does. Both Endgame tools open the device SHARED:
    //   CreateFileW(path, 0xC0000000, 3, NULL, 3, 0, NULL)   [D]
    //   share mode 3 = FILE_SHARE_READ|FILE_SHARE_WRITE
    //   updater-protocol.md:101, config-protocol.md:207
    // so engineering-rules.md §4.2's "mirror the vendor" points here, and hidapi's
    // exclusive default is the deviation. We exchange only FEATURE reports and
    // never read the input stream, so seizing buys us nothing and costs the
    // user their mouse for the duration.
    //
    // The tradeoff it accepts, recorded rather than hidden: nothing now stops a
    // second process interleaving with ours. The vendor accepts the same one --
    // share mode 3 across two separate executables -- and only the device can
    // say whether interleaving matters (working memory, flagged item #3).
    if (g_inited) hid_darwin_set_open_exclusive(0);
#endif
    return g_inited;
}

std::vector<Match> enumerateAll() {
    std::vector<Match> out;
    if (!initHid()) return out;
    hid_device_info* list = hid_enumerate(kVendorId, 0);
    for (hid_device_info* d = list; d; d = d->next) {
        out.push_back(Match{d->path ? d->path : "",
                            d->product_id, d->usage_page, d->usage,
                            d->release_number,
                            narrow(d->manufacturer_string),
                            narrow(d->product_string)});
    }
    hid_free_enumeration(list);
    return out;
}

std::vector<Match> findVendorCollection(std::uint16_t productId) {
    std::vector<Match> out;
    for (const Match& m : enumerateAll()) {
        // All four, always. Dropping any one of them is how a correct frame
        // reaches the wrong endpoint.
        if (m.productId == productId &&
            m.usagePage == kUsagePageVendor &&
            m.usage     == kUsageVendor) {
            out.push_back(m);
        }
    }
    return out;
}

std::unique_ptr<Device> Device::open(std::uint16_t productId, Log& log) {
    if (!initHid()) { log.warn("hid_init failed"); return nullptr; }

    std::vector<Match> hits = findVendorCollection(productId);

    if (hits.empty()) {
        // Say something useful rather than "not found". If the device is on the
        // bus at the OTHER product id, that is the single most likely reason and
        // the user can act on it.
        std::vector<Match> all = enumerateAll();
        if (all.empty()) {
            log.warn("no VID 0x3367 device is attached at all.");
        } else {
            char b[160];
            std::snprintf(b, sizeof b,
                "device is attached but no interface matched PID 0x%04x + "
                "UsagePage 0xFF01 + Usage 0x02.", productId);
            log.warn(b);
            for (const Match& m : all) {
                std::snprintf(b, sizeof b,
                    "  present: PID 0x%04x usagepage 0x%04x usage 0x%02x  %s %s",
                    m.productId, m.usagePage, m.usage,
                    m.manufacturer.c_str(), m.product.c_str());
                log.note(b);
            }
            if (productId == kProductIdApplication) {
                for (const Match& m : all)
                    if (m.productId == kProductIdBootloader)
                        log.note("the mouse is in BOOTLOADER mode. Unplug and "
                                 "replug it without holding any button.");
            }
        }
        return nullptr;
    }

    if (hits.size() > 1) {
        // build-design.md §1: fail loudly rather than take the first.
        char b[160];
        std::snprintf(b, sizeof b,
            "%zu interfaces match all four fields. Refusing to guess which one "
            "is the vendor collection.", hits.size());
        log.refused(b);
        for (const Match& m : hits) log.note("  " + m.path);
        return nullptr;
    }

    const Match& m = hits.front();
    hid_device* d = hid_open_path(m.path.c_str());
    if (!d) {
        // engineering-rules.md §2: "Detect missing macOS permissions and say so rather
        // than failing silently." Enumeration needs no permission; opening a
        // device this OS considers an input device may.
        //
        // There is more than one cause and we do not know which is common, so
        // this lists them without ranking them. An earlier version of this
        // message said the cause "is usually Input Monitoring" -- that was a
        // guess stated as a likelihood, and engineering-rules.md §1.2 does not allow it.
        // Whether Input Monitoring is needed at all is still device-gated and
        // unverified (§5).
        log.warn("the interface was found but could not be opened.");
        if (const wchar_t* e = hid_error(nullptr))
            log.note("hidapi says: " + narrow(e));

        // Distinguish "it went away" from "it is there and will not open".
        // Cheap, and it removes the biggest ambiguity for free.
        bool stillThere = false;
        for (const Match& x : enumerateAll())
            if (x.path == m.path) { stillThere = true; break; }

        if (!stillThere) {
            log.note("the interface has disappeared since enumeration -- the "
                     "device was probably unplugged or changed mode. Re-run.");
            return nullptr;
        }
        log.note("the interface is still present, so it is being refused "
                 "rather than missing.");
        log.note("MOST LIKELY: macOS Input Monitoring. Confirmed 2026-09-05 on "
                 "this device -- hidapi returned 0xE00002E2 kIOReturnNotPermitted "
                 "and tccd logged service=kTCCServiceListenEvent for egg-config. "
                 "macOS does NOT hand over the 0xFF01 vendor collection without "
                 "it. Root is not required and will not help.");
        log.note("  GRANT IT TO THE APP THAT OWNS YOUR TERMINAL WINDOW, not to "
                 "egg-config. tccd attributes the request to the RESPONSIBLE "
                 "process: Terminal.app, iTerm, or the editor if you launched "
                 "the shell from one. Granting the wrong one leaves the "
                 "permission visibly on and the open still failing.");
        log.note("  System Settings > Privacy & Security > Input Monitoring. "
                 "Then QUIT that app fully (Cmd-Q) and reopen it -- a running "
                 "process does not pick up a new grant.");
        log.note("  Confirm with:  log show --last 5m --predicate "
                 "'process == \"tccd\"' | grep -i ListenEvent");
        log.note("OTHER CAUSES, if the grant is in place and it still fails:");
        log.note("  - another process holds the device. Observed 2026-09-05: a "
                 "browser held this exact mouse open for exclusive access. That "
                 "reports 0xE00002C5 kIOReturnExclusiveAccess, NOT 0xE00002E2, "
                 "so the code above tells the two apart -- read it.");
        log.note("  - the OS has claimed it as an input device.");
        log.note("To see who holds it:  log show --last 5m --predicate "
                 "'eventMessage CONTAINS \"exclusive access\"'");
        return nullptr;
    }

    char b[200];
    std::snprintf(b, sizeof b,
        "opened PID 0x%04x  %s \"%s\"  version 0x%04x  usagepage 0x%04x usage 0x%02x",
        m.productId, m.manufacturer.c_str(), m.product.c_str(),
        m.releaseNumber, m.usagePage, m.usage);
    log.note(b);

    return std::unique_ptr<Device>(new Device(d, m));
}

Device::~Device() { if (dev_) hid_close(reinterpret_cast<hid_device*>(dev_)); }


// See Device.h for the derivation. Digit-wise, because the vendor's own decode
// is a hex-format-then-decimal-parse round trip and that is what makes each
// nibble a decimal digit.
bool decodeBcdVersion(std::uint16_t bcd, unsigned& major, unsigned& minor) {
    unsigned d[4];
    for (int i = 0; i < 4; ++i) {
        d[i] = (bcd >> (4 * (3 - i))) & 0xF;
        if (d[i] > 9) return false;      // formats as a letter; __wtol stops
    }
    // "%x" prints no leading zeros, so 0x0110 is "110" and __wtol reads 110.
    // The value is therefore the four digits as a decimal number, and the
    // caller's /100 splits it -- which is the same as taking the top two
    // digits and the bottom two, without ever touching a float.
    const unsigned n = d[0] * 1000 + d[1] * 100 + d[2] * 10 + d[3];
    major = n / 100;
    minor = n % 100;
    return true;
}

std::string describeVersion(std::uint16_t bcd) {
    unsigned major = 0, minor = 0;
    if (!decodeBcdVersion(bcd, major, minor)) return std::string();
    char b[32];
    std::snprintf(b, sizeof b, "%u.%02u", major, minor);
    return b;
}

}  // namespace egg
