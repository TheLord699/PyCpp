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
#include <map>
#include <random>
#include <set>
#include <tuple>
#include <vector>
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
    if (k == "IndexError") throw IndexError(message);
    if (k == "KeyError") throw KeyError(message);
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

// a[i]: negative indices count from the end, an index out of range raises IndexError
template <class C, class I>
decltype(auto) at(C&& c, I i) {
    using Plain = std::remove_cv_t<std::remove_reference_t<C>>;
    constexpr bool direct = std::is_pointer_v<Plain> || std::is_array_v<Plain> ||
                            !std::is_integral_v<I> || requires { typename Plain::mapped_type; };
    if constexpr (direct) {
        return (c[i]);
    } else {
        const long long n = static_cast<long long>(c.size());
        long long k = static_cast<long long>(i);
        if (k < 0) k += n;
        if (k < 0 || k >= n)
            fail("IndexError", "index " + std::to_string(static_cast<long long>(i)) +
                               " out of range (size " + std::to_string(n) + ")");
        return (c[static_cast<size_t>(k)]);
    }
}

// ===================== Python methods of str, list, dict and set =====================
// `obj.upper()` is generated as `pycpp::py_upper(obj)`. Each helper gives Python semantics
// on std::string / std::vector / std::map / std::set and otherwise calls the object's own
// member function, so a user class with a `pop` method keeps it.

template <class T> using plain_t = std::remove_cv_t<std::remove_reference_t<T>>;
template <class T> struct is_vector_t : std::false_type {};
template <class T, class A> struct is_vector_t<std::vector<T, A>> : std::true_type {};
template <class T> struct is_map_t : std::false_type {};
template <class K, class V, class C, class A> struct is_map_t<std::map<K, V, C, A>> : std::true_type {};
template <class T> struct is_set_t : std::false_type {};
template <class K, class C, class A> struct is_set_t<std::set<K, C, A>> : std::true_type {};
template <class T> inline constexpr bool is_vector_v = is_vector_t<plain_t<T>>::value;
template <class T> inline constexpr bool is_map_v = is_map_t<plain_t<T>>::value;
template <class T> inline constexpr bool is_set_v = is_set_t<plain_t<T>>::value;
template <class T> inline constexpr bool is_str_v = is_text<plain_t<T>> || std::is_same_v<plain_t<T>, char>;

template <class T> std::string as_text(const T& v) {
    if constexpr (std::is_same_v<plain_t<T>, char>) return std::string(1, v);
    else if constexpr (is_text<plain_t<T>>) return std::string(v);
    else return to_str(v);
}

inline bool is_py_space(unsigned char c) { return std::isspace(c) != 0; }

// The member call used when the object is not a built-in Python type
#define PYCPP_OR_MEMBER(name)                                                              \
    else if constexpr (requires { obj.name(std::forward<A>(args)...); }) {                 \
        return obj.name(std::forward<A>(args)...);                                         \
    }                                                                                      \
    else if constexpr (requires { obj->name(std::forward<A>(args)...); }) {                \
        return obj->name(std::forward<A>(args)...);                                        \
    }                                                                                      \
    else {                                                                                 \
        static_assert(sizeof(T) == 0, "this object has no method '" #name "'");            \
    }

#define PYCPP_STR_TRANSFORM(name, body)                                                    \
    template <class T, class... A> decltype(auto) py_##name(T&& obj, A&&... args) {         \
        if constexpr (is_str_v<T> && sizeof...(A) == 0) {                                  \
            std::string s = as_text(obj);                                                  \
            body                                                                           \
            return s;                                                                      \
        }                                                                                  \
        PYCPP_OR_MEMBER(name)                                                              \
    }

PYCPP_STR_TRANSFORM(upper, for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));)
PYCPP_STR_TRANSFORM(lower, for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));)
PYCPP_STR_TRANSFORM(swapcase, for (char& c : s) {
    const unsigned char u = static_cast<unsigned char>(c);
    c = static_cast<char>(std::isupper(u) ? std::tolower(u) : std::toupper(u));
})
PYCPP_STR_TRANSFORM(title, {
    bool start = true;
    for (char& c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalpha(u)) { c = static_cast<char>(start ? std::toupper(u) : std::tolower(u)); start = false; }
        else start = true;
    }
})
PYCPP_STR_TRANSFORM(capitalize, {
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = static_cast<char>(i == 0 ? std::toupper(static_cast<unsigned char>(s[i]))
                                        : std::tolower(static_cast<unsigned char>(s[i])));
})

#define PYCPP_STR_TEST(name, pred)                                                         \
    template <class T, class... A> decltype(auto) py_##name(T&& obj, A&&... args) {         \
        if constexpr (is_str_v<T> && sizeof...(A) == 0) {                                  \
            const std::string s = as_text(obj);                                            \
            if (s.empty()) return false;                                                   \
            for (unsigned char c : s) if (!(pred)) return false;                           \
            return true;                                                                   \
        }                                                                                  \
        PYCPP_OR_MEMBER(name)                                                              \
    }
PYCPP_STR_TEST(isdigit, std::isdigit(c))
PYCPP_STR_TEST(isalpha, std::isalpha(c))
PYCPP_STR_TEST(isalnum, std::isalnum(c))
PYCPP_STR_TEST(isspace, std::isspace(c))

template <class T, class... A> decltype(auto) py_isupper(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 0) {
        bool cased = false;
        for (unsigned char c : as_text(obj)) { if (std::islower(c)) return false; if (std::isupper(c)) cased = true; }
        return cased;
    }
    PYCPP_OR_MEMBER(isupper)
}
template <class T, class... A> decltype(auto) py_islower(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 0) {
        bool cased = false;
        for (unsigned char c : as_text(obj)) { if (std::isupper(c)) return false; if (std::islower(c)) cased = true; }
        return cased;
    }
    PYCPP_OR_MEMBER(islower)
}

// strip([chars]) / lstrip / rstrip
inline std::string strip_impl(const std::string& s, bool left, bool right, const std::string* chars) {
    auto drop = [&](char c) { return chars ? chars->find(c) != std::string::npos : is_py_space(static_cast<unsigned char>(c)); };
    size_t b = 0, e = s.size();
    if (left) while (b < e && drop(s[b])) ++b;
    if (right) while (e > b && drop(s[e - 1])) --e;
    return s.substr(b, e - b);
}
#define PYCPP_STRIP(name, l, r)                                                            \
    template <class T, class... A> decltype(auto) py_##name(T&& obj, A&&... args) {         \
        if constexpr (is_str_v<T> && sizeof...(A) == 0) return strip_impl(as_text(obj), l, r, nullptr); \
        else if constexpr (is_str_v<T> && sizeof...(A) == 1) {                             \
            const std::string chars = as_text(args...);                                    \
            return strip_impl(as_text(obj), l, r, &chars);                                 \
        }                                                                                  \
        PYCPP_OR_MEMBER(name)                                                              \
    }
PYCPP_STRIP(strip, true, true)
PYCPP_STRIP(lstrip, true, false)
PYCPP_STRIP(rstrip, false, true)

// split([sep[, maxsplit]])
inline std::vector<std::string> split_impl(const std::string& s, const std::string* sep, long long maxsplit) {
    std::vector<std::string> out;
    if (!sep) {   // runs of whitespace, no empty strings
        size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && is_py_space(static_cast<unsigned char>(s[i]))) ++i;
            if (i >= s.size()) break;
            if (maxsplit >= 0 && static_cast<long long>(out.size()) == maxsplit) {
                size_t e = s.size();
                while (e > i && is_py_space(static_cast<unsigned char>(s[e - 1]))) --e;
                out.push_back(s.substr(i, e - i));
                break;
            }
            size_t j = i;
            while (j < s.size() && !is_py_space(static_cast<unsigned char>(s[j]))) ++j;
            out.push_back(s.substr(i, j - i));
            i = j;
        }
        return out;
    }
    if (sep->empty()) fail("ValueError", "empty separator");
    size_t start = 0;
    while (true) {
        if (maxsplit >= 0 && static_cast<long long>(out.size()) == maxsplit) break;
        const size_t at = s.find(*sep, start);
        if (at == std::string::npos) break;
        out.push_back(s.substr(start, at - start));
        start = at + sep->size();
    }
    out.push_back(s.substr(start));
    return out;
}
template <class T, class... A> decltype(auto) py_split(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 0) return split_impl(as_text(obj), nullptr, -1);
    else if constexpr (is_str_v<T> && sizeof...(A) <= 2) {
        auto params = std::forward_as_tuple(args...);
        const std::string sep = as_text(std::get<0>(params));
        long long maxsplit = -1;
        if constexpr (sizeof...(A) == 2) maxsplit = static_cast<long long>(std::get<1>(params));
        return split_impl(as_text(obj), &sep, maxsplit);
    }
    PYCPP_OR_MEMBER(split)
}
template <class T, class... A> decltype(auto) py_rsplit(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) <= 2) {
        // split from the right: reverse, split with the reversed separator, reverse back
        std::string s = as_text(obj);
        std::reverse(s.begin(), s.end());
        std::vector<std::string> parts;
        if constexpr (sizeof...(A) == 0) parts = split_impl(s, nullptr, -1);
        else {
            auto params = std::forward_as_tuple(args...);
            std::string sep = as_text(std::get<0>(params));
            std::reverse(sep.begin(), sep.end());
            long long maxsplit = -1;
            if constexpr (sizeof...(A) == 2) maxsplit = static_cast<long long>(std::get<1>(params));
            parts = split_impl(s, &sep, maxsplit);
        }
        for (auto& p : parts) std::reverse(p.begin(), p.end());
        std::reverse(parts.begin(), parts.end());
        return parts;
    }
    PYCPP_OR_MEMBER(rsplit)
}
template <class T, class... A> decltype(auto) py_splitlines(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 0) {
        std::vector<std::string> out;
        const std::string s = as_text(obj);
        size_t start = 0;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\n' || s[i] == '\r') {
                out.push_back(s.substr(start, i - start));
                if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') ++i;
                start = i + 1;
            }
        }
        if (start < s.size()) out.push_back(s.substr(start));
        return out;
    }
    PYCPP_OR_MEMBER(splitlines)
}

// sep.join(items)
template <class T, class... A> decltype(auto) py_join(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 1) {
        const std::string sep = as_text(obj);
        std::string out;
        bool first = true;
        for (const auto& item : (args, ...)) {
            if (!first) out += sep;
            first = false;
            out += as_text(item);
        }
        return out;
    }
    PYCPP_OR_MEMBER(join)
}

// replace(old, new[, count])
template <class T, class... A> decltype(auto) py_replace(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && (sizeof...(A) == 2 || sizeof...(A) == 3)) {
        auto params = std::forward_as_tuple(args...);
        const std::string from = as_text(std::get<0>(params)), to = as_text(std::get<1>(params));
        long long count = -1;
        if constexpr (sizeof...(A) == 3) count = static_cast<long long>(std::get<2>(params));
        std::string s = as_text(obj), out;
        if (from.empty()) {   // Python inserts `to` between every character
            for (size_t i = 0; i < s.size(); ++i) {
                if (count < 0 || static_cast<long long>(i) < count) out += to;
                out += s[i];
            }
            if (count < 0 || static_cast<long long>(s.size()) < count) out += to;
            return out;
        }
        size_t start = 0;
        long long done = 0;
        while (count < 0 || done < count) {
            const size_t at = s.find(from, start);
            if (at == std::string::npos) break;
            out += s.substr(start, at - start) + to;
            start = at + from.size();
            ++done;
        }
        return out + s.substr(start);
    }
    PYCPP_OR_MEMBER(replace)
}

template <class T, class... A> decltype(auto) py_find(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 1) {
        const size_t at = as_text(obj).find(as_text(args...));
        return at == std::string::npos ? -1 : static_cast<int>(at);
    }
    PYCPP_OR_MEMBER(find)
}
template <class T, class... A> decltype(auto) py_rfind(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 1) {
        const size_t at = as_text(obj).rfind(as_text(args...));
        return at == std::string::npos ? -1 : static_cast<int>(at);
    }
    PYCPP_OR_MEMBER(rfind)
}
template <class T, class... A> decltype(auto) py_index(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 1) {
        const size_t at = as_text(obj).find(as_text(args...));
        if (at == std::string::npos) fail("ValueError", "substring not found");
        return static_cast<int>(at);
    }
    else if constexpr (is_vector_v<T> && sizeof...(A) == 1) {
        for (size_t i = 0; i < obj.size(); ++i)
            if (obj[i] == (args, ...)) return static_cast<int>(i);
        fail("ValueError", to_repr((args, ...)) + " is not in list");
    }
    PYCPP_OR_MEMBER(index)
}
template <class T, class... A> decltype(auto) py_count(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 1) {
        const std::string s = as_text(obj), sub = as_text(args...);
        if (sub.empty()) return static_cast<int>(s.size() + 1);
        int n = 0;
        for (size_t at = s.find(sub); at != std::string::npos; at = s.find(sub, at + sub.size())) ++n;
        return n;
    }
    else if constexpr (is_vector_v<T> && sizeof...(A) == 1) {
        return static_cast<int>(std::count(obj.begin(), obj.end(), (args, ...)));
    }
    PYCPP_OR_MEMBER(count)
}
template <class T, class... A> decltype(auto) py_startswith(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 1) {
        const std::string s = as_text(obj), p = as_text(args...);
        return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
    }
    PYCPP_OR_MEMBER(startswith)
}
template <class T, class... A> decltype(auto) py_endswith(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 1) {
        const std::string s = as_text(obj), p = as_text(args...);
        return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
    }
    PYCPP_OR_MEMBER(endswith)
}

// center/ljust/rjust(width[, fill]), zfill(width)
inline std::string pad_impl(const std::string& s, long long width, char fill, int where) {   // -1 left, 0 center, 1 right
    if (width <= static_cast<long long>(s.size())) return s;
    const size_t total = static_cast<size_t>(width) - s.size();
    size_t left = where < 0 ? 0 : where > 0 ? total : total / 2 + (total & static_cast<size_t>(width) & 1);
    return std::string(left, fill) + s + std::string(total - left, fill);
}
#define PYCPP_PAD(name, where)                                                             \
    template <class T, class... A> decltype(auto) py_##name(T&& obj, A&&... args) {         \
        if constexpr (is_str_v<T> && (sizeof...(A) == 1 || sizeof...(A) == 2)) {          \
            auto params = std::forward_as_tuple(args...);                                  \
            char fill = ' ';                                                               \
            if constexpr (sizeof...(A) == 2) {                                             \
                const std::string f = as_text(std::get<1>(params));                        \
                if (f.size() != 1) fail("TypeError", "the fill character must be exactly one character long"); \
                fill = f[0];                                                               \
            }                                                                              \
            return pad_impl(as_text(obj), static_cast<long long>(std::get<0>(params)), fill, where); \
        }                                                                                  \
        PYCPP_OR_MEMBER(name)                                                              \
    }
PYCPP_PAD(center, 0)
PYCPP_PAD(ljust, -1)
PYCPP_PAD(rjust, 1)
template <class T, class... A> decltype(auto) py_zfill(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 1) {
        std::string s = as_text(obj);
        const long long width = static_cast<long long>((args, ...));
        if (width <= static_cast<long long>(s.size())) return s;
        const size_t sign = (!s.empty() && (s[0] == '-' || s[0] == '+')) ? 1 : 0;
        s.insert(sign, std::string(static_cast<size_t>(width) - s.size(), '0'));
        return s;
    }
    PYCPP_OR_MEMBER(zfill)
}

// ---- list ----
template <class T, class... A> decltype(auto) py_append(T&& obj, A&&... args) {
    if constexpr (is_vector_v<T> && sizeof...(A) == 1) obj.push_back(std::forward<A>(args)...);
    PYCPP_OR_MEMBER(append)
}
template <class T, class... A> decltype(auto) py_extend(T&& obj, A&&... args) {
    if constexpr (is_vector_v<T> && sizeof...(A) == 1) {
        auto copy = plain_t<T>((args, ...).begin(), (args, ...).end());   // safe for xs.extend(xs)
        obj.insert(obj.end(), copy.begin(), copy.end());
    }
    PYCPP_OR_MEMBER(extend)
}
template <class T, class... A> decltype(auto) py_insert(T&& obj, A&&... args) {
    if constexpr (is_vector_v<T> && sizeof...(A) == 2) {
        auto params = std::forward_as_tuple(args...);
        long long i = static_cast<long long>(std::get<0>(params));
        const long long n = static_cast<long long>(obj.size());
        if (i < 0) i = std::max(0LL, i + n);
        if (i > n) i = n;
        obj.insert(obj.begin() + i, std::get<1>(params));
    }
    PYCPP_OR_MEMBER(insert)
}
template <class T, class... A> decltype(auto) py_pop(T&& obj, A&&... args) {
    if constexpr (is_vector_v<T> && sizeof...(A) <= 1) {
        if (obj.empty()) fail("IndexError", "pop from empty list");
        long long i = static_cast<long long>(obj.size()) - 1;
        if constexpr (sizeof...(A) == 1) {
            i = static_cast<long long>((args, ...));
            if (i < 0) i += static_cast<long long>(obj.size());
            if (i < 0 || i >= static_cast<long long>(obj.size())) fail("IndexError", "pop index out of range");
        }
        auto value = std::move(obj[static_cast<size_t>(i)]);
        obj.erase(obj.begin() + i);
        return value;
    }
    else if constexpr (is_map_v<T> && (sizeof...(A) == 1 || sizeof...(A) == 2)) {
        auto params = std::forward_as_tuple(args...);
        auto it = obj.find(std::get<0>(params));
        if (it == obj.end()) {
            if constexpr (sizeof...(A) == 2) return typename plain_t<T>::mapped_type(std::get<1>(params));
            else fail("KeyError", to_repr(std::get<0>(params)));
        }
        auto value = std::move(it->second);
        obj.erase(it);
        return value;
    }
    PYCPP_OR_MEMBER(pop)
}
template <class T, class... A> decltype(auto) py_remove(T&& obj, A&&... args) {
    if constexpr (is_vector_v<T> && sizeof...(A) == 1) {
        auto it = std::find(obj.begin(), obj.end(), (args, ...));
        if (it == obj.end()) fail("ValueError", "list.remove(x): x not in list");
        obj.erase(it);
    }
    else if constexpr (is_set_v<T> && sizeof...(A) == 1) {
        if (obj.erase((args, ...)) == 0) fail("KeyError", to_repr((args, ...)));
    }
    PYCPP_OR_MEMBER(remove)
}
template <class T, class... A> decltype(auto) py_clear(T&& obj, A&&... args) {
    if constexpr ((is_vector_v<T> || is_map_v<T> || is_set_v<T>) && sizeof...(A) == 0) obj.clear();
    PYCPP_OR_MEMBER(clear)
}
template <class T, class... A> decltype(auto) py_sort(T&& obj, A&&... args) {
    if constexpr (is_vector_v<T> && sizeof...(A) == 0) std::sort(obj.begin(), obj.end());
    PYCPP_OR_MEMBER(sort)
}
template <class T, class... A> decltype(auto) py_reverse(T&& obj, A&&... args) {
    if constexpr (is_vector_v<T> && sizeof...(A) == 0) std::reverse(obj.begin(), obj.end());
    PYCPP_OR_MEMBER(reverse)
}
template <class T, class... A> decltype(auto) py_copy(T&& obj, A&&... args) {
    if constexpr ((is_vector_v<T> || is_map_v<T> || is_set_v<T>) && sizeof...(A) == 0) return plain_t<T>(obj);
    PYCPP_OR_MEMBER(copy)
}

// ---- dict ----
template <class T, class... A> decltype(auto) py_get(T&& obj, A&&... args) {
    if constexpr (is_map_v<T> && (sizeof...(A) == 1 || sizeof...(A) == 2)) {
        using V = typename plain_t<T>::mapped_type;
        auto params = std::forward_as_tuple(args...);
        auto it = obj.find(std::get<0>(params));
        if (it != obj.end()) return V(it->second);
        if constexpr (sizeof...(A) == 2) return V(std::get<1>(params));
        else return V{};   // Python returns None; PyCPP returns the value type's default
    }
    PYCPP_OR_MEMBER(get)
}
template <class T, class... A> decltype(auto) py_keys(T&& obj, A&&... args) {
    if constexpr (is_map_v<T> && sizeof...(A) == 0) {
        std::vector<typename plain_t<T>::key_type> out;
        for (const auto& kv : obj) out.push_back(kv.first);
        return out;
    }
    PYCPP_OR_MEMBER(keys)
}
template <class T, class... A> decltype(auto) py_values(T&& obj, A&&... args) {
    if constexpr (is_map_v<T> && sizeof...(A) == 0) {
        std::vector<typename plain_t<T>::mapped_type> out;
        for (const auto& kv : obj) out.push_back(kv.second);
        return out;
    }
    PYCPP_OR_MEMBER(values)
}
template <class T, class... A> decltype(auto) py_items(T&& obj, A&&... args) {
    if constexpr (is_map_v<T> && sizeof...(A) == 0) {
        std::vector<std::pair<typename plain_t<T>::key_type, typename plain_t<T>::mapped_type>> out(obj.begin(), obj.end());
        return out;
    }
    PYCPP_OR_MEMBER(items)
}
template <class T, class... A> decltype(auto) py_update(T&& obj, A&&... args) {
    if constexpr (is_map_v<T> && sizeof...(A) == 1) {
        for (const auto& kv : (args, ...)) obj.insert_or_assign(kv.first, kv.second);
    }
    PYCPP_OR_MEMBER(update)
}
template <class T, class... A> decltype(auto) py_setdefault(T&& obj, A&&... args) {
    if constexpr (is_map_v<T> && sizeof...(A) == 2) {
        auto params = std::forward_as_tuple(args...);
        return (obj.try_emplace(std::get<0>(params), std::get<1>(params)).first->second);
    }
    PYCPP_OR_MEMBER(setdefault)
}

// ---- set ----
template <class T, class... A> decltype(auto) py_add(T&& obj, A&&... args) {
    if constexpr (is_set_v<T> && sizeof...(A) == 1) obj.insert(std::forward<A>(args)...);
    PYCPP_OR_MEMBER(add)
}
template <class T, class... A> decltype(auto) py_discard(T&& obj, A&&... args) {
    if constexpr (is_set_v<T> && sizeof...(A) == 1) obj.erase((args, ...));
    PYCPP_OR_MEMBER(discard)
}

#undef PYCPP_OR_MEMBER
#undef PYCPP_STR_TRANSFORM
#undef PYCPP_STR_TEST
#undef PYCPP_STRIP
#undef PYCPP_PAD

// ===================== f-string format specs =====================
// [[fill]align][sign][#][0][width][,|_][.precision][type], as in Python's format().

struct FormatSpec {
    char fill = ' ', align = 0, sign = '-', type = 0, grouping = 0;
    bool alternate = false;
    long long width = -1, precision = -1;
};

inline FormatSpec parse_format_spec(const std::string& spec) {
    FormatSpec f;
    size_t i = 0;
    auto is_align = [](char c) { return c == '<' || c == '>' || c == '^' || c == '='; };
    if (spec.size() >= 2 && is_align(spec[1])) { f.fill = spec[0]; f.align = spec[1]; i = 2; }
    else if (!spec.empty() && is_align(spec[0])) { f.align = spec[0]; i = 1; }
    if (i < spec.size() && (spec[i] == '+' || spec[i] == '-' || spec[i] == ' ')) f.sign = spec[i++];
    if (i < spec.size() && spec[i] == '#') { f.alternate = true; ++i; }
    if (i < spec.size() && spec[i] == '0') {
        if (!f.align) { f.fill = '0'; f.align = '='; }
        ++i;
    }
    if (i < spec.size() && std::isdigit(static_cast<unsigned char>(spec[i]))) {
        f.width = 0;
        while (i < spec.size() && std::isdigit(static_cast<unsigned char>(spec[i]))) f.width = f.width * 10 + (spec[i++] - '0');
    }
    if (i < spec.size() && (spec[i] == ',' || spec[i] == '_')) f.grouping = spec[i++];
    if (i < spec.size() && spec[i] == '.') {
        ++i;
        f.precision = 0;
        if (i >= spec.size() || !std::isdigit(static_cast<unsigned char>(spec[i])))
            fail("ValueError", "format specifier missing precision");
        while (i < spec.size() && std::isdigit(static_cast<unsigned char>(spec[i]))) f.precision = f.precision * 10 + (spec[i++] - '0');
    }
    if (i < spec.size()) f.type = spec[i++];
    if (i != spec.size()) fail("ValueError", "invalid format specifier '" + spec + "'");
    return f;
}

// 1234567 -> 1,234,567 (the digits before any '.' or exponent)
inline std::string group_digits(const std::string& digits, char sep, size_t every = 3) {
    size_t end = digits.find_first_of(".eE");
    if (end == std::string::npos) end = digits.size();
    std::string head = digits.substr(0, end), out;
    for (size_t i = 0; i < head.size(); ++i) {
        if (i > 0 && (head.size() - i) % every == 0) out += sep;
        out += head[i];
    }
    return out + digits.substr(end);
}

// Pad `body` (without sign) to the width, honouring alignment and sign-aware '=' padding.
inline std::string pad_formatted(const FormatSpec& f, const std::string& sign, const std::string& body, char default_align) {
    const std::string whole = sign + body;
    if (f.width < 0 || static_cast<long long>(whole.size()) >= f.width) return whole;
    const size_t pad = static_cast<size_t>(f.width) - whole.size();
    switch (f.align ? f.align : default_align) {
    case '<': return whole + std::string(pad, f.fill);
    case '^': return std::string(pad / 2, f.fill) + whole + std::string(pad - pad / 2, f.fill);
    case '=': return sign + std::string(pad, f.fill) + body;
    default:  return std::string(pad, f.fill) + whole;
    }
}

inline std::string format_float(double v, const FormatSpec& f) {
    std::string sign = std::signbit(v) && !std::isnan(v) ? "-" : (f.sign == '+' ? "+" : f.sign == ' ' ? " " : "");
    v = std::fabs(v);
    char type = f.type;
    std::string body;
    if (std::isinf(v) || std::isnan(v)) body = std::isnan(v) ? "nan" : "inf";
    else if (type == 0 && f.precision < 0) body = float_to_str(v);
    else {
        const long long precision = f.precision < 0 ? 6 : f.precision;
        double value = v;
        bool percent = false;
        if (type == '%') { value *= 100; type = 'f'; percent = true; }
        if (type == 0) type = 'g';
        if (type != 'f' && type != 'F' && type != 'e' && type != 'E' && type != 'g' && type != 'G')
            fail("ValueError", std::string("unknown format code '") + f.type + "' for a float");
        char fmt[16];
        std::snprintf(fmt, sizeof fmt, "%%%s.%lld%c", f.alternate ? "#" : "", precision, type);
        char buf[512];
        std::snprintf(buf, sizeof buf, fmt, value);
        body = buf;
        if (percent) body += '%';
    }
    if (f.grouping) body = group_digits(body, f.grouping);
    return pad_formatted(f, sign, body, '>');
}

template <class T>
std::string format_spec(const T& value, const std::string& spec) {
    if (spec.empty()) return to_str(value);
    const FormatSpec f = parse_format_spec(spec);
    if constexpr (std::is_same_v<T, bool>) {
        if (f.type == 0 || f.type == 's') return pad_formatted(f, "", to_str(value), '<');
        return format_spec(static_cast<long long>(value), spec);
    }
    else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, char>) {
        if (f.type == 'e' || f.type == 'E' || f.type == 'f' || f.type == 'F' || f.type == 'g' ||
            f.type == 'G' || f.type == '%')
            return format_float(static_cast<double>(value), f);
        if (f.precision >= 0) fail("ValueError", "precision not allowed in integer format specifier");
        const bool negative = value < 0;
        unsigned long long u = negative ? 0ULL - static_cast<unsigned long long>(value) : static_cast<unsigned long long>(value);
        std::string sign = negative ? "-" : (f.sign == '+' ? "+" : f.sign == ' ' ? " " : "");
        std::string body, prefix;
        unsigned base = 10;
        const char* digits = "0123456789abcdef";
        switch (f.type) {
        case 0: case 'd': case 'n': break;
        case 'b': base = 2; prefix = "0b"; break;
        case 'o': base = 8; prefix = "0o"; break;
        case 'x': base = 16; prefix = "0x"; break;
        case 'X': base = 16; prefix = "0X"; digits = "0123456789ABCDEF"; break;
        case 'c': return pad_formatted(f, "", std::string(1, static_cast<char>(u)), '<');
        default: fail("ValueError", std::string("unknown format code '") + f.type + "' for an integer");
        }
        do { body.insert(body.begin(), digits[u % base]); u /= base; } while (u != 0);
        if (f.grouping) body = group_digits(body, f.grouping, base == 10 ? 3 : 4);
        if (f.alternate) sign += prefix;
        return pad_formatted(f, sign, body, '>');
    }
    else if constexpr (std::is_floating_point_v<T>) {
        return format_float(static_cast<double>(value), f);
    }
    else {
        if (f.type != 0 && f.type != 's') fail("ValueError", std::string("unknown format code '") + f.type + "' for a string");
        std::string text = as_text(value);
        if (f.precision >= 0 && static_cast<long long>(text.size()) > f.precision) text.resize(static_cast<size_t>(f.precision));
        return pad_formatted(f, "", text, '<');
    }
}

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
