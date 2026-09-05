// ConfigLink.h -- the one seam between the config session and the world.
//
// The flasher has had this since the beginning (BootloaderLink) and the config
// tool did not, which meant every safety property CLAUDE.md §4.1 asks for --
// "never write after a failed read", "validate a read is structurally plausible
// before acting on it", "never let a reported success stand in for verifying
// the data itself" -- lived in `int cmdSet(...)` inside main.cpp and could only
// be exercised by owning the mouse and running the binary.
//
// That is exactly backwards. §4.3 names the adversarial mock "the highest-value
// artefact in the project" and says it "needs no hardware". A device that lies
// about a write is the failure this tool exists to survive, and it was the one
// failure nothing could reproduce.
#pragma once

#include "egg/Transport.h"

#include <cstdint>
#include <string>
#include <vector>

namespace egg::cfg {

// Everything the session may do to a device. Three operations, because the
// derivation found three config commands that matter and no code path may
// invent a fourth.
//
// Each returns egg::Reply rather than bool, for the reason Transport.h already
// gives: a transport failure and a device status are different things, and the
// vendor's own wrappers conflate them (updater-protocol.md §3.4a). Ours cannot.
class ConfigLink {
public:
    virtual ~ConfigLink() = default;

    // A1 12 -> the 1041-byte A0 settings record.
    virtual Reply readRecord() = 0;

    // A0 11 carrying a full 1041-byte frame -> the small A1 acknowledgement.
    // The frame is passed whole, including its report id, because the session
    // is responsible for the exact bytes on the wire and must not delegate
    // that to something it cannot see.
    virtual Reply writeRecord(const std::vector<std::uint8_t>& frame) = 0;

    // A1 13. Both the config tool's Factory Reset button and the updater's
    // last act send this byte-for-byte (updater-protocol.md §5.4a), so it is
    // one operation here too.
    virtual Reply factoryReset() = 0;

    // Somewhere to narrate. §3 keeps the tools CLI-first so output is a log by
    // construction.
    virtual void note(const std::string& line) = 0;
};

}  // namespace egg::cfg
