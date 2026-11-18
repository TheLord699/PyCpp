#ifndef PYCPP_RUNTIME_TEXT_H
#define PYCPP_RUNTIME_TEXT_H
#include <filesystem>
#include <fstream>

// Written next to the generated files whenever generated code uses it.
inline const char* const kPycppRuntime = R"PYCPP(#ifndef PYCPP_RUNTIME_H
#define PYCPP_RUNTIME_H
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace pycpp {

[[noreturn]] inline void fail(const char* kind, const std::string& message) {
    std::fprintf(stderr, "%s: %s\n", kind, message.c_str());
    std::exit(1);
}

template <class T>
inline constexpr bool is_text = std::is_convertible_v<const T&, std::string>;

inline bool only_blank(const std::string& s, size_t from) {
    for (size_t i = from; i < s.size(); ++i)
        if (!std::isspace(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

template <class R = int, class T>
R to_int(const T& v) {
    if constexpr (is_text<T>) {
        const std::string s(v);
        try {
            size_t used = 0;
            const long long n = std::stoll(s, &used);
            if (only_blank(s, used)) {
                if (n >= std::numeric_limits<R>::min() && n <= std::numeric_limits<R>::max())
                    return static_cast<R>(n);
                fail("OverflowError", "'" + s + "' is out of range for this integer type");
            }
        } catch (const std::exception&) {}
        fail("ValueError", "invalid literal for int(): '" + s + "'");
    } else {
        return static_cast<R>(v);
    }
}

template <class T>
double to_double(const T& v) {
    if constexpr (is_text<T>) {
        const std::string s(v);
        try {
            size_t used = 0;
            const double d = std::stod(s, &used);
            if (only_blank(s, used)) return d;
        } catch (const std::exception&) {}
        fail("ValueError", "could not convert string to float: '" + s + "'");
    } else {
        return static_cast<double>(v);
    }
}

template <class T>
float to_float(const T& v) { return static_cast<float>(to_double(v)); }

template <class T>
bool to_bool(const T& v) {
    if constexpr (is_text<T>) return !std::string(v).empty();   // Python semantics
    else return static_cast<bool>(v);
}

// For `flag: bool = input(...)`
inline bool parse_bool(const std::string& raw) {
    std::string s = raw;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    if (s == "true" || s == "1" || s == "yes" || s == "y") return true;
    if (s == "false" || s == "0" || s == "no" || s == "n" || s.empty()) return false;
    fail("ValueError", "cannot convert '" + raw + "' to bool");
}

template <class T>
std::string to_str(const T& v) {
    if constexpr (is_text<T>) return std::string(v);
    else if constexpr (std::is_same_v<T, bool>) return v ? "True" : "False";
    else if constexpr (std::is_same_v<T, char>) return std::string(1, v);
    else if constexpr (std::is_integral_v<T>) return std::to_string(v);
    else if constexpr (std::is_floating_point_v<T>) {
        char buf[48];
        for (int p = 1; p <= 17; ++p) {                // shortest text that round-trips
            std::snprintf(buf, sizeof buf, "%.*g", p, static_cast<double>(v));
            if (static_cast<T>(std::strtod(buf, nullptr)) == v) break;
        }
        std::string s = buf;
        if (s.find_first_of(".en") == std::string::npos) s += ".0";
        return s;
    } else {
        std::ostringstream o; o << v; return o.str();
    }
}

// `defer stmt` -> runs when the enclosing scope exits (return, break, end of block).
template <class F>
class Defer {
    F fn;
public:
    explicit Defer(F f) : fn(std::move(f)) {}
    Defer(const Defer&) = delete;
    Defer& operator=(const Defer&) = delete;
    ~Defer() { fn(); }
};
template <class F> Defer(F) -> Defer<F>;

} // namespace pycpp
#endif
)PYCPP";

inline void writeRuntimeHeader(std::filesystem::path dir) {
    if (dir.empty()) dir = ".";
    std::ofstream(dir / "pycpp_runtime.h", std::ios::binary) << kPycppRuntime;
}
#endif
