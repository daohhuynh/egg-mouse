#include "egg/Log.h"

#include <cstdio>
#include <cctype>

namespace egg {

std::string hexDump(const std::uint8_t* p, std::size_t n, std::size_t indent) {
    std::string out;
    char line[128];
    const std::string pad(indent, ' ');
    for (std::size_t i = 0; i < n; i += 16) {
        out += pad;
        std::snprintf(line, sizeof line, "%04zx  ", i);
        out += line;
        for (std::size_t j = 0; j < 16; ++j) {
            if (i + j < n) std::snprintf(line, sizeof line, "%02x ", p[i + j]);
            else           std::snprintf(line, sizeof line, "   ");
            out += line;
            if (j == 7) out += ' ';
        }
        out += " |";
        for (std::size_t j = 0; j < 16 && i + j < n; ++j) {
            unsigned char c = p[i + j];
            out += (std::isprint(c) ? static_cast<char>(c) : '.');
        }
        out += "|\n";
    }
    return out;
}

void Log::frame(Dir d, const std::vector<std::uint8_t>& buf, const char* what) {
    const char* arrow = (d == Dir::Out) ? "-->" : "<--";
    // The header line carries the two bytes that decide everything: the report
    // id and byte 1, which is the command going out and the status coming back.
    std::printf("%s %-28s len=%-5zu id=0x%02x b1=0x%02x\n",
                arrow, what, buf.size(),
                buf.empty() ? 0 : buf[0],
                buf.size() > 1 ? buf[1] : 0);
    if (verbose_ && !buf.empty())
        std::fputs(hexDump(buf.data(), buf.size()).c_str(), stdout);
    std::fflush(stdout);
}

void Log::note(const std::string& s)    { std::printf("    %s\n", s.c_str()); std::fflush(stdout); }
void Log::warn(const std::string& s)    { std::printf("!!  %s\n", s.c_str()); std::fflush(stdout); }
void Log::refused(const std::string& s) { std::printf("XX  REFUSED: %s\n", s.c_str()); std::fflush(stdout); }

}  // namespace egg
