// Log.h -- every frame that crosses the wire, recorded.
//
// CLAUDE.md §3: the flasher is CLI-first so that "output is a log by
// construction". §4.1 requires the diff of every write to be logged. This is
// the one place that happens, so that a session can be replayed and diffed
// against a golden capture (§4.3) without re-running anything.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace egg {

enum class Dir { Out, In };

class Log {
public:
    // verbose: also hex-dump full frame bodies, not just the header line.
    explicit Log(bool verbose = false) : verbose_(verbose) {}

    void frame(Dir d, const std::vector<std::uint8_t>& buf, const char* what);
    void note(const std::string& s);
    void warn(const std::string& s);

    // A frame the code decided NOT to send, and why. A refusal is as much a
    // protocol event as a send, and it is the one an audit most wants to see.
    void refused(const std::string& why);

    bool verbose() const { return verbose_; }

private:
    bool verbose_;
};

// Canonical hex dump: 16 bytes per line, offset then hex then ASCII. Used for
// frames and for the settings record, so a record printed by egg-config and a
// frame printed by egg-flash line up byte-for-byte when compared by eye.
std::string hexDump(const std::uint8_t* p, std::size_t n, std::size_t indent = 2);

}  // namespace egg
