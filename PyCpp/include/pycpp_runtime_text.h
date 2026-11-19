#ifndef PYCPP_RUNTIME_TEXT_H
#define PYCPP_RUNTIME_TEXT_H
#include <filesystem>
#include <fstream>

// Written next to the generated files whenever generated code uses it.
inline const char* const kPycppRuntime = R"PYCPP(#ifndef PYCPP_RUNTIME_H
#define PYCPP_RUNTIME_H
#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace pycpp {

// PyCPP exceptions. They deliberately do not derive from std::exception, so the runtime's
// own `catch (const std::exception&)` blocks (e.g. around std::stoll) never swallow them.
class Exception {
public:
    Exception() = default;
    explicit Exception(std::string message) : msg_(std::move(message)) {}
    virtual ~Exception() = default;
    const char* what() const noexcept { return msg_.c_str(); }
    virtual const char* name() const { return "Exception"; }
private:
    std::string msg_;
};
class ArithmeticError : public Exception { public: using Exception::Exception; const char* name() const override { return "ArithmeticError"; } };
class ZeroDivisionError : public ArithmeticError { public: using ArithmeticError::ArithmeticError; const char* name() const override { return "ZeroDivisionError"; } };
class OverflowError : public ArithmeticError { public: using ArithmeticError::ArithmeticError; const char* name() const override { return "OverflowError"; } };
class LookupError : public Exception { public: using Exception::Exception; const char* name() const override { return "LookupError"; } };
class IndexError : public LookupError { public: using LookupError::LookupError; const char* name() const override { return "IndexError"; } };
class KeyError : public LookupError { public: using LookupError::LookupError; const char* name() const override { return "KeyError"; } };
class ValueError : public Exception { public: using Exception::Exception; const char* name() const override { return "ValueError"; } };
class TypeError : public Exception { public: using Exception::Exception; const char* name() const override { return "TypeError"; } };
class RuntimeError : public Exception { public: using Exception::Exception; const char* name() const override { return "RuntimeError"; } };

// Errors raised by the runtime itself (int("x"), 1 / 0, ...). Catchable with try/except.
[[noreturn]] inline void fail(const char* kind, const std::string& message) {
    const std::string k = kind;
    if (k == "ZeroDivisionError") throw ZeroDivisionError(message);
    if (k == "OverflowError") throw OverflowError(message);
    if (k == "ValueError") throw ValueError(message);
    if (k == "TypeError") throw TypeError(message);
    throw RuntimeError(message);
}

// An exception nobody caught: print "ValueError: message" and exit 1, as before.
inline const bool terminate_handler_installed = [] {
    std::set_terminate([] {
        if (auto active = std::current_exception()) {
            try { std::rethrow_exception(active); }
            catch (const Exception& e) { std::fprintf(stderr, "%s: %s\n", e.name(), e.what()); std::exit(1); }
            catch (const std::exception& e) { std::fprintf(stderr, "error: %s\n", e.what()); std::exit(1); }
            catch (...) {}
        }
        std::abort();
    });
    return true;
}();

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

// Shortest text that reads back as the same value, in Python's style:
// 100.0, 0.1, 3.5, 1e-05, 1e+16, inf, nan
template <class T>
std::string float_to_str(T v) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v < 0 ? "-inf" : "inf";
    char buf[64];
    for (int p = 0; p <= 17; ++p) {
        std::snprintf(buf, sizeof buf, "%.*e", p, static_cast<double>(v));
        if (static_cast<T>(std::strtod(buf, nullptr)) == v) break;
    }
    std::string s = buf;                                  // e.g. -1.5e+02
    const bool neg = s[0] == '-';
    if (neg) s.erase(0, 1);
    const size_t epos = s.find('e');
    const int exp10 = std::atoi(s.c_str() + epos + 1);
    std::string digits;
    for (size_t i = 0; i < epos; ++i) if (s[i] != '.') digits += s[i];
    std::string out;
    if (exp10 < -4 || exp10 >= 16) {
        out = digits.substr(0, 1);
        if (digits.size() > 1) out += "." + digits.substr(1);
        char tail[16];
        std::snprintf(tail, sizeof tail, "e%c%02d", exp10 < 0 ? '-' : '+', exp10 < 0 ? -exp10 : exp10);
        out += tail;
    } else if (exp10 >= 0) {
        const size_t whole = static_cast<size_t>(exp10) + 1;
        if (digits.size() < whole) digits.append(whole - digits.size(), '0');
        out = digits.substr(0, whole) + "." + (digits.size() > whole ? digits.substr(whole) : std::string("0"));
    } else {
        out = "0." + std::string(static_cast<size_t>(-exp10 - 1), '0') + digits;
    }
    return neg ? "-" + out : out;
}

template <class T> std::string to_str(const T& v);
template <class T> std::string to_repr(const T& v);
template <class C> std::string container_str(const C& c);

template <class T>
std::string to_str(const T& v) {
    if constexpr (is_text<T>) return std::string(v);
    else if constexpr (std::is_same_v<T, bool>) return v ? "True" : "False";
    else if constexpr (std::is_base_of_v<Exception, T>) return v.what();
    else if constexpr (std::is_same_v<T, char>) return std::string(1, v);
    else if constexpr (std::is_integral_v<T>) return std::to_string(v);
    else if constexpr (std::is_floating_point_v<T>) {
        return float_to_str(v);
    } else {
        if constexpr (requires { v.begin(); v.end(); }) return container_str(v);
        else { std::ostringstream o; o << v; return o.str(); }
    }
}

// Text of an element inside a container: strings are quoted, like Python's repr()
template <class T>
std::string to_repr(const T& v) {
    if constexpr (is_text<T>) return "'" + std::string(v) + "'";
    else if constexpr (std::is_same_v<T, char>) return "'" + std::string(1, v) + "'";
    else return to_str(v);
}

// [1, 2, 3]  /  {'a': 1}  /  {1, 2}
template <class C>
std::string container_str(const C& c) {
    constexpr bool is_map = requires { typename C::mapped_type; };
    constexpr bool is_set = requires { typename C::key_type; } && !is_map;
    std::string out = (is_map || is_set) ? "{" : "[";
    bool first = true;
    for (const auto& item : c) {
        if (!first) out += ", ";
        first = false;
        if constexpr (is_map) out += to_repr(item.first) + ": " + to_repr(item.second);
        else out += to_repr(item);
    }
    out += (is_map || is_set) ? "}" : "]";
    return out;
}

// a / b: true division. Integers give a double (7 / 2 == 3.5).
template <class A, class B>
constexpr auto truediv(A a, B b) {
    if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
        if (b == 0) fail("ZeroDivisionError", "division by zero");
        return static_cast<double>(a) / static_cast<double>(b);
    } else {
        return a / b;
    }
}

// a // b: floor division (rounds toward negative infinity)
template <class A, class B>
constexpr auto floordiv(A a, B b) {
    if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
        if (b == 0) fail("ZeroDivisionError", "integer division by zero");
        auto q = a / b;
        if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
        return q;
    } else {
        if (b == 0) fail("ZeroDivisionError", "float floor division by zero");
        return std::floor(static_cast<double>(a) / static_cast<double>(b));
    }
}

// a % b: the result has the sign of b, like Python
template <class A, class B>
constexpr auto mod(A a, B b) {
    if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
        if (b == 0) fail("ZeroDivisionError", "integer modulo by zero");
        auto r = a % b;
        if (r != 0 && ((r < 0) != (b < 0))) r += b;
        return r;
    } else {
        if (b == 0) fail("ZeroDivisionError", "float modulo by zero");
        auto r = std::fmod(a, b);
        if (r != 0 && ((r < 0) != (b < 0))) r += b;
        return r;
    }
}

// a ** b: integers stay integers
template <class A, class B>
constexpr auto power(A a, B b) {
    if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
        if (b < 0) return static_cast<long long>(std::pow(static_cast<double>(a), static_cast<double>(b)));
        long long result = 1, base = static_cast<long long>(a);
        auto e = b;
        while (e > 0) { if (e & 1) result *= base; base *= base; e >>= 1; }
        return result;
    } else {
        return std::pow(a, b);
    }
}

// bin()/oct()/hex(): Python-style text such as "0b101" or "-0xff"
template <class T>
std::string to_base(T value, unsigned base, const char* prefix) {
    unsigned long long u = static_cast<unsigned long long>(value);
    bool negative = false;
    if constexpr (std::is_signed_v<T>) {
        if (value < 0) { negative = true; u = 0ULL - static_cast<unsigned long long>(value); }
    }
    std::string digits;
    do { digits.push_back("0123456789abcdef"[u % base]); u /= base; } while (u != 0);
    std::reverse(digits.begin(), digits.end());
    return std::string(negative ? "-" : "") + prefix + digits;
}

// randint(lo, hi): both ends included
inline int randint(int lo, int hi) {
    static std::mt19937 gen{std::random_device{}()};
    if (lo > hi) std::swap(lo, hi);
    return std::uniform_int_distribution<int>(lo, hi)(gen);
}

// `x in container`: substring test for text, key test for map/set, element test otherwise
template <class C, class T>
bool contains(const C& c, const T& v) {
    if constexpr (is_text<C>) {
        const std::string hay(c);
        if constexpr (is_text<T>) return hay.find(std::string(v)) != std::string::npos;
        else return hay.find(v) != std::string::npos;
    } else if constexpr (requires { c.find(v); }) {
        return c.find(v) != c.end();
    } else {
        return std::find(c.begin(), c.end(), v) != c.end();
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

using pycpp::Exception;
using pycpp::ArithmeticError;
using pycpp::ZeroDivisionError;
using pycpp::OverflowError;
using pycpp::LookupError;
using pycpp::IndexError;
using pycpp::KeyError;
using pycpp::ValueError;
using pycpp::TypeError;
using pycpp::RuntimeError;
#endif
)PYCPP";

inline void writeRuntimeHeader(std::filesystem::path dir) {
    if (dir.empty()) dir = ".";
    std::ofstream(dir / "pycpp_runtime.h", std::ios::binary) << kPycppRuntime;
}
#endif
