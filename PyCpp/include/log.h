#ifndef PYCPP_LOG_H
#define PYCPP_LOG_H

#include <iostream>

// Diagnostic output for the compiler's own internals (token streams, parser
// steps, analyzer decisions). It is silent unless -v / --verbose is given;
// real errors and warnings never go through here.
namespace pycpp {

inline bool& verboseFlag() {
    static bool enabled = false;
    return enabled;
}

inline void setVerbose(bool enabled) { verboseFlag() = enabled; }

class NullBuffer : public std::streambuf {
protected:
    int overflow(int c) override { return c; }
};

inline std::ostream& debugLog() {
    static NullBuffer null_buffer;
    static std::ostream null_stream(&null_buffer);
    return verboseFlag() ? std::cout : null_stream;
}

} // namespace pycpp

#endif // PYCPP_LOG_H
