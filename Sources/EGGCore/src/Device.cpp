#include "egg/Device.h"

#include <hidapi.h>

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
        // CLAUDE.md §2: "Detect missing macOS permissions and say so rather
        // than failing silently." Enumeration needs no permission; opening a
        // device this OS considers an input device may.
        //
        // There is more than one cause and we do not know which is common, so
        // this lists them without ranking them. An earlier version of this
        // message said the cause "is usually Input Monitoring" -- that was a
        // guess stated as a likelihood, and CLAUDE.md §1.2 does not allow it.
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
                 "rather than missing. Known causes, in no particular order:");
        log.note("  1. another process holds the device. Observed 2026-09-05: "
                 "a browser held this exact mouse open for exclusive access "
                 "and the kernel refused another process's open. Quit browsers "
                 "and any vendor or web configurator, then re-run.");
        log.note("  2. macOS Input Monitoring. Grant it to your terminal in "
                 "System Settings > Privacy & Security > Input Monitoring. "
                 "Root is not required and will not help.");
        log.note("  3. the OS has claimed it as an input device.");
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

}  // namespace egg
