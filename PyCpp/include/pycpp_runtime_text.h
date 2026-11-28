#ifndef PYCPP_RUNTIME_TEXT_H
#define PYCPP_RUNTIME_TEXT_H
#include <filesystem>
#include <fstream>

// Written next to the generated files whenever generated code uses it.
inline const char* const kPycppRuntime = R"PYCPP(#ifndef PYCPP_RUNTIME_H
#define PYCPP_RUNTIME_H

// PyCPP C++20 runtime.
// Optimized for common paths while retaining Python-like behavior and
// member-function fallback for user-defined types.

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(__GNUG__)
#include <cxxabi.h>
#endif

namespace pycpp {

// -----------------------------------------------------------------------------
// Exceptions
// -----------------------------------------------------------------------------

class Exception {
public:
    Exception() = default;
    explicit Exception(std::string message) : message_(std::move(message)) {}
    virtual ~Exception() = default;

    const char* what() const noexcept { return message_.c_str(); }
    virtual const char* name() const noexcept { return "Exception"; }

private:
    std::string message_;
};

#define PYCPP_EXCEPTION(type, base)                                      \
    class type : public base {                                            \
    public:                                                               \
        using base::base;                                                 \
        const char* name() const noexcept override { return #type; }      \
    }

PYCPP_EXCEPTION(ArithmeticError, Exception);
PYCPP_EXCEPTION(ZeroDivisionError, ArithmeticError);
PYCPP_EXCEPTION(OverflowError, ArithmeticError);
PYCPP_EXCEPTION(LookupError, Exception);
PYCPP_EXCEPTION(IndexError, LookupError);
PYCPP_EXCEPTION(KeyError, LookupError);
PYCPP_EXCEPTION(ValueError, Exception);
PYCPP_EXCEPTION(TypeError, Exception);
PYCPP_EXCEPTION(RuntimeError, Exception);

#undef PYCPP_EXCEPTION

[[noreturn]] inline void fail(const char* kind, const std::string& message) {
    if (std::strcmp(kind, "ZeroDivisionError") == 0) throw ZeroDivisionError(message);
    if (std::strcmp(kind, "OverflowError") == 0) throw OverflowError(message);
    if (std::strcmp(kind, "ValueError") == 0) throw ValueError(message);
    if (std::strcmp(kind, "TypeError") == 0) throw TypeError(message);
    if (std::strcmp(kind, "IndexError") == 0) throw IndexError(message);
    if (std::strcmp(kind, "KeyError") == 0) throw KeyError(message);
    throw RuntimeError(message);
}

// Install once per program. PyCPP exceptions intentionally do not derive from
// std::exception, so internal std::exception handlers cannot swallow them.
inline const bool terminate_handler_installed = [] {
    std::set_terminate([] {
        if (auto active = std::current_exception()) {
            try {
                std::rethrow_exception(active);
            } catch (const Exception& e) {
                std::fprintf(stderr, "%s: %s\n", e.name(), e.what());
                std::exit(1);
            } catch (const std::exception& e) {
                std::fprintf(stderr, "error: %s\n", e.what());
                std::exit(1);
            } catch (...) {
            }
        }
        std::abort();
    });
    return true;
}();

// -----------------------------------------------------------------------------
// Type traits and basic conversions
// -----------------------------------------------------------------------------

template<class T>
using plain_t = std::remove_cv_t<std::remove_reference_t<T>>;

template<class T>
inline constexpr bool is_text = std::is_convertible_v<const T&, std::string>;

template<class T> struct is_tuple_t : std::false_type {};
template<class... T> struct is_tuple_t<std::tuple<T...>> : std::true_type {};
template<class T> inline constexpr bool is_tuple_v = is_tuple_t<plain_t<T>>::value;

template<class T> struct is_vector_t : std::false_type {};
template<class T, class A> struct is_vector_t<std::vector<T, A>> : std::true_type {};
template<class T> struct is_map_t : std::false_type {};
template<class K, class V, class C, class A>
struct is_map_t<std::map<K, V, C, A>> : std::true_type {};
template<class T> struct is_set_t : std::false_type {};
template<class K, class C, class A>
struct is_set_t<std::set<K, C, A>> : std::true_type {};

template<class T> inline constexpr bool is_vector_v = is_vector_t<plain_t<T>>::value;
template<class T> inline constexpr bool is_map_v = is_map_t<plain_t<T>>::value;
template<class T> inline constexpr bool is_set_v = is_set_t<plain_t<T>>::value;
template<class T>
inline constexpr bool is_str_v =
    is_text<plain_t<T>> || std::is_same_v<plain_t<T>, char>;

inline bool only_blank(std::string_view s, std::size_t from) noexcept {
    for (std::size_t i = from; i < s.size(); ++i)
        if (!std::isspace(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

template<class R = int, class T>
R to_int(const T& value) {
    if constexpr (is_text<T>) {
        const std::string s(value);
        const char* first = s.data();
        const char* last = first + s.size();

        while (first != last && std::isspace(static_cast<unsigned char>(*first)))
            ++first;

        bool positive_sign = false;
        if (first != last && *first == '+') {
            positive_sign = true;
            ++first;
        }

        R result{};
        const auto parsed = std::from_chars(first, last, result, 10);
        if (parsed.ec == std::errc() && only_blank(s, static_cast<std::size_t>(parsed.ptr - s.data())))
            return result;

        if (parsed.ec == std::errc::result_out_of_range)
            fail("OverflowError", "'" + s + "' is out of range for this integer type");

        (void)positive_sign;
        fail("ValueError", "invalid literal for int(): '" + s + "'");
    } else {
        if constexpr (std::is_integral_v<T> && std::is_integral_v<R> &&
                      !std::is_same_v<plain_t<T>, bool> &&
                      !std::is_same_v<plain_t<R>, bool>) {
            if (!std::in_range<R>(value))
                fail("OverflowError", "integer value is out of range");
        }
        return static_cast<R>(value);
    }
}

template<class T>
double to_double(const T& value) {
    if constexpr (is_text<T>) {
        const std::string s(value);
        const char* begin = s.data();
        const char* end = begin + s.size();
        while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) ++begin;

        double result{};
        const auto parsed = std::from_chars(begin, end, result, std::chars_format::general);
        if (parsed.ec == std::errc() &&
            only_blank(std::string_view(s), static_cast<std::size_t>(parsed.ptr - s.data())))
            return result;

        // from_chars implementations may reject "inf"/"nan" variants; accept
        // the spellings supported by Python's float() through a small fallback.
        std::string lower;
        lower.reserve(s.size());
        for (unsigned char c : s) lower.push_back(static_cast<char>(std::tolower(c)));
        if (lower == "inf" || lower == "+inf" || lower == "infinity" || lower == "+infinity")
            return std::numeric_limits<double>::infinity();
        if (lower == "-inf" || lower == "-infinity")
            return -std::numeric_limits<double>::infinity();
        if (lower == "nan" || lower == "+nan" || lower == "-nan")
            return std::numeric_limits<double>::quiet_NaN();

        fail("ValueError", "could not convert string to float: '" + s + "'");
    } else {
        return static_cast<double>(value);
    }
}

template<class T>
float to_float(const T& value) { return static_cast<float>(to_double(value)); }

template<class T>
bool to_bool(const T& value) {
    if constexpr (is_text<T>) return !std::string(value).empty();
    else if constexpr (requires { value.size(); value.begin(); }) return !value.empty();
    else return static_cast<bool>(value);
}

inline bool parse_bool(const std::string& raw) {
    std::string s;
    s.reserve(raw.size());
    for (unsigned char c : raw) s.push_back(static_cast<char>(std::tolower(c)));
    if (s == "true" || s == "1" || s == "yes" || s == "y") return true;
    if (s == "false" || s == "0" || s == "no" || s == "n" || s.empty()) return false;
    fail("ValueError", "cannot convert '" + raw + "' to bool");
}

// -----------------------------------------------------------------------------
// Stringification: append-first to avoid temporary strings for nested values
// -----------------------------------------------------------------------------

template<class T> std::string to_str(const T& value);
template<class T> std::string to_repr(const T& value);
template<class C> void append_container(std::string& out, const C& container);
template<class T> void append_repr(std::string& out, const T& value);

template<class T>
std::string float_to_str(T value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return std::signbit(value) ? "-inf" : "inf";

    char buffer[128];
    const auto result = std::to_chars(
        buffer, buffer + sizeof(buffer), static_cast<double>(value),
        std::chars_format::general);

    std::string s;
    if (result.ec == std::errc()) {
        s.assign(buffer, result.ptr);
    } else {
        const int n = std::snprintf(buffer, sizeof(buffer), "%.17g",
                                   static_cast<double>(value));
        s.assign(buffer, n > 0 ? static_cast<std::size_t>(n) : 0);
    }

    // Python repr switches to scientific notation only when the decimal exponent
    // is below -4 or at least 16. std::to_chars(general) may choose scientific
    // notation based on the shortest significand length instead, so when it emits
    // an exponent inside Python's decimal range we expand the value back to fixed
    // notation. This relies on to_chars(general) emitting exponential form as
    // "d[.ddd]e±NN" (one digit before the point), which the standard guarantees
    // for the shortest round-trippable representation.
    const auto epos = s.find_first_of("eE");
    if (epos != std::string::npos) {
        const int exponent = std::atoi(s.c_str() + epos + 1);
        if (exponent >= -4 && exponent < 16) {
            std::string mantissa = s.substr(0, epos);
            const bool negative = !mantissa.empty() && mantissa.front() == '-';
            const std::size_t begin = negative ? 1 : 0;
            const auto dot = mantissa.find('.', begin);
            const std::size_t decimal_pos = dot == std::string::npos
                ? mantissa.size() : dot;
            std::string digits;
            for (std::size_t i = begin; i < mantissa.size(); ++i)
                if (mantissa[i] != '.') digits.push_back(mantissa[i]);
            const long long point = static_cast<long long>(decimal_pos - begin) + exponent;
            std::string fixed;
            if (negative) fixed.push_back('-');
            if (point <= 0) {
                fixed += "0.";
                fixed.append(static_cast<std::size_t>(-point), '0');
                fixed += digits;
            } else if (static_cast<std::size_t>(point) >= digits.size()) {
                fixed += digits;
                fixed.append(static_cast<std::size_t>(point) - digits.size(), '0');
                fixed += ".0";
            } else {
                fixed.append(digits.data(), static_cast<std::size_t>(point));
                fixed.push_back('.');
                fixed.append(digits.data() + point,
                             digits.size() - static_cast<std::size_t>(point));
            }
            s = std::move(fixed);
        }
    }

    if (s.find_first_of(".eE") == std::string::npos) s += ".0";
    const auto e = s.find('e');
    if (e != std::string::npos) {
        if (e + 1 < s.size() && s[e + 1] != '+' && s[e + 1] != '-')
            s.insert(e + 1, 1, '+');
        const std::size_t sign = (e + 1 < s.size() &&
            (s[e + 1] == '+' || s[e + 1] == '-')) ? e + 2 : e + 1;
        if (s.size() - sign == 1) s.insert(sign, 1, '0');
    }
    return s;
}

template<class T>
void append_str(std::string& out, const T& value) {
    if constexpr (is_text<T>) {
        out.append(std::string_view(value));
    } else if constexpr (std::is_same_v<plain_t<T>, char>) {
        out.push_back(value);
    } else if constexpr (std::is_same_v<plain_t<T>, bool>) {
        out += value ? "True" : "False";
    } else if constexpr (std::is_integral_v<T>) {
        char buffer[std::numeric_limits<T>::digits10 + 4];
        const auto r = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (r.ec == std::errc())
            out.append(buffer, static_cast<std::size_t>(r.ptr - buffer));
        else
            out += std::to_string(value);
    } else if constexpr (std::is_floating_point_v<T>) {
        out += float_to_str(value);
    } else if constexpr (is_tuple_v<T>) {
        out.push_back('(');
        std::apply([&](const auto&... item) {
            std::size_t index = 0;
            ((out += (index++ ? ", " : ""), append_repr(out, item)), ...);
        }, value);
        if constexpr (std::tuple_size_v<plain_t<T>> == 1) out.push_back(',');
        out.push_back(')');
    } else if constexpr (requires { value.begin(); value.end(); }) {
        append_container(out, value);
    } else if constexpr (std::is_base_of_v<Exception, plain_t<T>>) {
        out += value.what();
    } else {
        std::ostringstream stream;
        stream << value;
        out += stream.str();
    }
}

template<class T>
void append_repr(std::string& out, const T& value) {
    if constexpr (is_text<T> || std::is_same_v<plain_t<T>, char>) {
        out.push_back('\'');
        if constexpr (is_text<T>) out.append(std::string_view(value));
        else out.push_back(value);
        out.push_back('\'');
    } else {
        append_str(out, value);
    }
}

template<class C>
void append_container(std::string& out, const C& container) {
    constexpr bool map_like = requires { typename C::mapped_type; };
    constexpr bool set_like = requires { typename C::key_type; } && !map_like;

    out.push_back((map_like || set_like) ? '{' : '[');
    bool first = true;
    for (const auto& item : container) {
        if (!first) out += ", ";
        first = false;
        if constexpr (map_like) {
            append_repr(out, item.first);
            out += ": ";
            append_repr(out, item.second);
        } else {
            append_repr(out, item);
        }
    }
    out.push_back((map_like || set_like) ? '}' : ']');
}

template<class T>
std::string to_str(const T& value) {
    std::string out;
    if constexpr (requires { value.size(); } && !is_text<T>)
        out.reserve(static_cast<std::size_t>(value.size()) * 8 + 2);
    append_str(out, value);
    return out;
}

template<class T>
std::string to_repr(const T& value) {
    std::string out;
    append_repr(out, value);
    return out;
}

template<class T>
std::string as_text(const T& value) {
    if constexpr (std::is_same_v<plain_t<T>, char>) return std::string(1, value);
    else if constexpr (is_text<T>) return std::string(value);
    else return to_str(value);
}

// -----------------------------------------------------------------------------
// Arithmetic
// -----------------------------------------------------------------------------

template<class A, class B>
constexpr auto truediv(A a, B b) {
    if (b == 0) fail("ZeroDivisionError", "division by zero");
    if constexpr (std::is_integral_v<A> && std::is_integral_v<B>)
        return static_cast<double>(a) / static_cast<double>(b);
    else
        return a / b;
}

template<class A, class B>
constexpr auto floordiv(A a, B b) {
    if (b == 0) fail("ZeroDivisionError",
        (std::is_integral_v<A> && std::is_integral_v<B>)
            ? "integer division by zero" : "float floor division by zero");

    if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
        using R = decltype(a / b);
        if constexpr (std::is_signed_v<A> && std::is_signed_v<B>) {
            if (a == std::numeric_limits<A>::lowest() && b == -1)
                fail("OverflowError", "integer division result too large");
        }
        R q = a / b;
        const R r = a % b;
        if (r != 0 && ((a < 0) != (b < 0))) --q;
        return q;
    } else {
        return std::floor(static_cast<double>(a) / static_cast<double>(b));
    }
}

template<class A, class B>
constexpr auto mod(A a, B b) {
    if (b == 0) fail("ZeroDivisionError", "modulo by zero");
    if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
        if constexpr (std::is_signed_v<A> && std::is_signed_v<B>) {
            if (a == std::numeric_limits<A>::lowest() && b == -1) return A{0};
        }
        auto r = a % b;
        if (r != 0 && ((r < 0) != (b < 0))) r += b;
        return r;
    } else {
        auto r = std::fmod(a, b);
        if (r != 0 && ((r < 0) != (b < 0))) r += b;
        return r;
    }
}

template<class A, class B>
long long power(A a, B b) {
    if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
        if (b < 0)
            return static_cast<long long>(std::pow(static_cast<double>(a), static_cast<double>(b)));
        using U = std::make_unsigned_t<std::common_type_t<A, B>>;
        U exponent = static_cast<U>(b);
        long long result = 1;
        long long base = static_cast<long long>(a);
        const auto checked_mul = [](long long x, long long y, long long& out) {
            if (x == 0 || y == 0) { out = 0; return true; }
            if ((x == -1 && y == std::numeric_limits<long long>::lowest()) ||
                (y == -1 && x == std::numeric_limits<long long>::lowest())) return false;
            if (x > 0) {
                if (y > 0) { if (x > std::numeric_limits<long long>::max() / y) return false; }
                else { if (y < std::numeric_limits<long long>::lowest() / x) return false; }
            } else {
                if (y > 0) { if (x < std::numeric_limits<long long>::lowest() / y) return false; }
                else { if (x < std::numeric_limits<long long>::max() / y) return false; }
            }
            out = x * y;
            return true;
        };
        while (exponent) {
            if (exponent & 1u) {
                long long next{};
                if (!checked_mul(result, base, next)) fail("OverflowError", "integer power overflow");
                result = next;
            }
            exponent >>= 1u;
            if (exponent) {
                long long next{};
                if (!checked_mul(base, base, next)) fail("OverflowError", "integer power overflow");
                base = next;
            }
        }
        return result;
    } else {
        return static_cast<long long>(std::pow(a, b));
    }
}

template<class T>
std::string to_base(T value, unsigned base, const char* prefix) {
    using U = std::make_unsigned_t<T>;
    bool negative = false;
    U number = static_cast<U>(value);
    if constexpr (std::is_signed_v<T>) {
        if (value < 0) {
            negative = true;
            number = U(0) - number;
        }
    }
    char buffer[std::numeric_limits<U>::digits + 1];
    char* end = buffer + sizeof(buffer);
    char* p = end;
    do {
        *--p = "0123456789abcdef"[number % base];
        number /= base;
    } while (number);
    return std::string(negative ? "-" : "") + prefix + std::string(p, end);
}

inline int randint(int lo, int hi) {
    static thread_local std::mt19937 generator{std::random_device{}()};
    if (lo > hi) std::swap(lo, hi);
    return std::uniform_int_distribution<int>(lo, hi)(generator);
}

// -----------------------------------------------------------------------------
// Generic helpers
// -----------------------------------------------------------------------------

template<class C, class T>
bool contains(const C& container, const T& value) {
    if constexpr (is_text<C>) {
        const std::string_view hay(container);
        if constexpr (is_text<T>) return hay.find(std::string_view(value)) != std::string_view::npos;
        else return hay.find(value) != std::string_view::npos;
    } else if constexpr (requires { container.find(value); }) {
        return container.find(value) != container.end();
    } else {
        return std::find(container.begin(), container.end(), value) != container.end();
    }
}

template<class F>
class Defer {
    F function_;
public:
    explicit Defer(F function) : function_(std::move(function)) {}
    Defer(const Defer&) = delete;
    Defer& operator=(const Defer&) = delete;
    Defer(Defer&& other) noexcept(std::is_nothrow_move_constructible_v<F>)
        : function_(std::move(other.function_)), active_(other.active_) {
        other.active_ = false;
    }
    ~Defer() noexcept(noexcept(std::declval<F&>()())) {
        if (active_) function_();
    }
private:
    bool active_ = true;
};
template<class F> Defer(F) -> Defer<F>;

template<class C, class I>
decltype(auto) at(C&& container, I index) {
    using P = plain_t<C>;
    constexpr bool dictionary = requires { typename P::mapped_type; };
    constexpr bool direct = std::is_pointer_v<P> || std::is_array_v<P> ||
                            !std::is_integral_v<I>;
    if constexpr (dictionary) {
        auto it = container.find(index);
        if (it == container.end()) fail("KeyError", to_repr(index));
        return (it->second);
    } else if constexpr (direct) {
        return container[index];
    } else {
        const auto n = static_cast<long long>(container.size());
        long long i = static_cast<long long>(index);
        if (i < 0) i += n;
        if (i < 0 || i >= n)
            fail("IndexError", "index " + std::to_string(static_cast<long long>(index)) +
                 " out of range (size " + std::to_string(n) + ")");
        return (container[static_cast<std::size_t>(i)]);
    }
}

template<class C, class I>
decltype(auto) at_w(C&& container, I index) {
    using P = plain_t<C>;
    if constexpr (requires { typename P::mapped_type; }) return (container[index]);
    else return at(std::forward<C>(container), index);
}

// -----------------------------------------------------------------------------
// Built-in Python-like methods + custom member fallback
// -----------------------------------------------------------------------------

#define PYCPP_OR_MEMBER(name)                                                     \
    else if constexpr (requires { obj.name(std::forward<A>(args)...); }) {        \
        return obj.name(std::forward<A>(args)...);                                \
    } else if constexpr (requires { obj->name(std::forward<A>(args)...); }) {     \
        return obj->name(std::forward<A>(args)...);                               \
    } else {                                                                       \
        static_assert(sizeof(T) == 0, "this object has no method '" #name "'");   \
    }

#define PYCPP_STR_TRANSFORM(name, body)                                            \
    template<class T, class... A> decltype(auto) py_##name(T&& obj, A&&... args) { \
        if constexpr (is_str_v<T> && sizeof...(A) == 0) {                          \
            std::string s = as_text(obj);                                          \
            body                                                                   \
            return s;                                                              \
        }                                                                          \
        PYCPP_OR_MEMBER(name)                                                      \
    }

PYCPP_STR_TRANSFORM(upper,
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));)
PYCPP_STR_TRANSFORM(lower,
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));)
PYCPP_STR_TRANSFORM(swapcase,
    for (char& c : s) { const auto u = static_cast<unsigned char>(c);
        c = static_cast<char>(std::isupper(u) ? std::tolower(u) : std::toupper(u)); })
PYCPP_STR_TRANSFORM(title,
    bool start = true;
    for (char& c : s) { const auto u = static_cast<unsigned char>(c);
        if (std::isalpha(u)) { c = static_cast<char>(start ? std::toupper(u) : std::tolower(u)); start = false; }
        else start = true; })
PYCPP_STR_TRANSFORM(capitalize,
    for (std::size_t i = 0; i < s.size(); ++i)
        s[i] = static_cast<char>(i == 0 ? std::toupper(static_cast<unsigned char>(s[i]))
                                       : std::tolower(static_cast<unsigned char>(s[i])));)

#define PYCPP_STR_TEST(name, predicate)                                            \
    template<class T, class... A> decltype(auto) py_##name(T&& obj, A&&... args) {  \
        if constexpr (is_str_v<T> && sizeof...(A) == 0) {                          \
            const std::string s = as_text(obj);                                    \
            if (s.empty()) return false;                                           \
            for (unsigned char c : s) if (!(predicate)) return false;              \
            return true;                                                           \
        }                                                                          \
        PYCPP_OR_MEMBER(name)                                                      \
    }

PYCPP_STR_TEST(isdigit, std::isdigit(c))
PYCPP_STR_TEST(isalpha, std::isalpha(c))
PYCPP_STR_TEST(isalnum, std::isalnum(c))
PYCPP_STR_TEST(isspace, std::isspace(c))

template<class T, class... A>
decltype(auto) py_isupper(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 0) {
        bool cased = false;
        for (unsigned char c : as_text(obj)) {
            if (std::islower(c)) return false;
            if (std::isupper(c)) cased = true;
        }
        return cased;
    }
    PYCPP_OR_MEMBER(isupper)
}
template<class T, class... A>
decltype(auto) py_islower(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 0) {
        bool cased = false;
        for (unsigned char c : as_text(obj)) {
            if (std::isupper(c)) return false;
            if (std::islower(c)) cased = true;
        }
        return cased;
    }
    PYCPP_OR_MEMBER(islower)
}

inline bool is_py_space(unsigned char c) noexcept { return std::isspace(c) != 0; }

inline std::string strip_impl(const std::string& s, bool left, bool right,
                              const std::string* chars) {
    const auto drop = [&](char c) {
        return chars ? chars->find(c) != std::string::npos
                     : is_py_space(static_cast<unsigned char>(c));
    };
    std::size_t begin = 0, end = s.size();
    if (left) while (begin < end && drop(s[begin])) ++begin;
    if (right) while (end > begin && drop(s[end - 1])) --end;
    return s.substr(begin, end - begin);
}

#define PYCPP_STRIP(name, left, right)                                             \
    template<class T, class... A> decltype(auto) py_##name(T&& obj, A&&... args) { \
        if constexpr (is_str_v<T> && sizeof...(A) == 0)                           \
            return strip_impl(as_text(obj), left, right, nullptr);                \
        else if constexpr (is_str_v<T> && sizeof...(A) == 1) {                    \
            const std::string chars = as_text((args, ...));                       \
            return strip_impl(as_text(obj), left, right, &chars);                 \
        }                                                                          \
        PYCPP_OR_MEMBER(name)                                                      \
    }
PYCPP_STRIP(strip, true, true)
PYCPP_STRIP(lstrip, true, false)
PYCPP_STRIP(rstrip, false, true)

inline std::vector<std::string> split_impl(std::string_view s,
        const std::string* separator, long long maxsplit) {
    std::vector<std::string> out;
    if (!separator) {
        std::size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && is_py_space(static_cast<unsigned char>(s[i]))) ++i;
            if (i == s.size()) break;
            if (maxsplit >= 0 && static_cast<long long>(out.size()) == maxsplit) {
                std::size_t end = s.size();
                while (end > i && is_py_space(static_cast<unsigned char>(s[end - 1]))) --end;
                out.emplace_back(s.substr(i, end - i));
                break;
            }
            std::size_t j = i;
            while (j < s.size() && !is_py_space(static_cast<unsigned char>(s[j]))) ++j;
            out.emplace_back(s.substr(i, j - i));
            i = j;
        }
        return out;
    }
    if (separator->empty()) fail("ValueError", "empty separator");
    std::size_t start = 0;
    while (maxsplit < 0 || static_cast<long long>(out.size()) < maxsplit) {
        const std::size_t pos = s.find(*separator, start);
        if (pos == std::string_view::npos) break;
        out.emplace_back(s.substr(start, pos - start));
        start = pos + separator->size();
    }
    out.emplace_back(s.substr(start));
    return out;
}

template<class T, class... A>
decltype(auto) py_split(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 0)
        return split_impl(as_text(obj), nullptr, -1);
    else if constexpr (is_str_v<T> && sizeof...(A) <= 2) {
        auto params = std::forward_as_tuple(args...);
        const std::string sep = as_text(std::get<0>(params));
        long long maxsplit = -1;
        if constexpr (sizeof...(A) == 2) maxsplit = static_cast<long long>(std::get<1>(params));
        return split_impl(as_text(obj), &sep, maxsplit);
    }
    PYCPP_OR_MEMBER(split)
}

template<class T, class... A>
decltype(auto) py_rsplit(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) <= 2) {
        std::string s = as_text(obj);
        std::string sep;
        long long maxsplit = -1;
        if constexpr (sizeof...(A) != 0) {
            auto params = std::forward_as_tuple(args...);
            sep = as_text(std::get<0>(params));
            if constexpr (sizeof...(A) == 2) maxsplit = static_cast<long long>(std::get<1>(params));
        }
        if (sizeof...(A) == 0) {
            auto parts = split_impl(s, nullptr, -1);
            return parts;
        }
        if (sep.empty()) fail("ValueError", "empty separator");
        std::vector<std::string> reversed;
        std::size_t end = s.size();
        while (maxsplit < 0 || static_cast<long long>(reversed.size()) < maxsplit) {
            const std::size_t pos = s.rfind(sep, end);
            if (pos == std::string::npos) break;
            reversed.emplace_back(s.substr(pos + sep.size(), end - pos - sep.size()));
            end = pos;
        }
        std::reverse(reversed.begin(), reversed.end());
        reversed.insert(reversed.begin(), s.substr(0, end));
        return reversed;
    }
    PYCPP_OR_MEMBER(rsplit)
}

template<class T, class... A>
decltype(auto) py_splitlines(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 0) {
        std::vector<std::string> out;
        const std::string s = as_text(obj);
        std::size_t start = 0;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\n' || s[i] == '\r') {
                out.emplace_back(s.substr(start, i - start));
                if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') ++i;
                start = i + 1;
            }
        }
        if (start < s.size()) out.emplace_back(s.substr(start));
        return out;
    }
    PYCPP_OR_MEMBER(splitlines)
}

template<class T, class... A>
decltype(auto) py_join(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 1) {
        const std::string separator = as_text(obj);
        const auto& items = (args, ...);
        std::size_t total = items.size() > 1 ? (items.size() - 1) * separator.size() : 0;
        for (const auto& item : items) total += as_text(item).size();
        std::string out;
        out.reserve(total);
        bool first = true;
        for (const auto& item : items) {
            if (!first) out += separator;
            first = false;
            out += as_text(item);
        }
        return out;
    }
    PYCPP_OR_MEMBER(join)
}

inline std::string replace_impl(const std::string& source, const std::string& from,
                                const std::string& to, long long count) {
    if (from.empty()) {
        const std::size_t n = count < 0 ? source.size() + 1
            : std::min(source.size() + 1, static_cast<std::size_t>(count));
        if (to.size() && n > (std::numeric_limits<std::size_t>::max() - source.size()) / to.size())
            fail("OverflowError", "replacement result too large");
        std::string out;
        out.reserve(source.size() + n * to.size());
        for (std::size_t i = 0; i <= source.size(); ++i) {
            if (i < n) out += to;
            if (i < source.size()) out.push_back(source[i]);
        }
        return out;
    }

    std::vector<std::size_t> positions;
    std::size_t pos = 0;
    while (count < 0 || static_cast<long long>(positions.size()) < count) {
        pos = source.find(from, pos);
        if (pos == std::string::npos) break;
        positions.push_back(pos);
        pos += from.size();
    }

    std::string out;
    if (to.size() >= from.size()) {
        const std::size_t growth = to.size() - from.size();
        if (growth && positions.size() >
            (std::numeric_limits<std::size_t>::max() - source.size()) / growth)
            fail("OverflowError", "replacement result too large");
        out.reserve(source.size() + positions.size() * growth);
    } else {
        out.reserve(source.size() - positions.size() * (from.size() - to.size()));
    }

    pos = 0;
    for (const auto at : positions) {
        out.append(source, pos, at - pos);
        out += to;
        pos = at + from.size();
    }
    out.append(source, pos, std::string::npos);
    return out;
}

template<class T, class... A>
decltype(auto) py_replace(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && (sizeof...(A) == 2 || sizeof...(A) == 3)) {
        auto p = std::forward_as_tuple(args...);
        const std::string from = as_text(std::get<0>(p));
        const std::string to = as_text(std::get<1>(p));
        long long count = -1;
        if constexpr (sizeof...(A) == 3) count = static_cast<long long>(std::get<2>(p));
        return replace_impl(as_text(obj), from, to, count);
    }
    PYCPP_OR_MEMBER(replace)
}

template<class T, class... A>
decltype(auto) py_find(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 1) {
        const auto pos = as_text(obj).find(as_text((args, ...)));
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }
    PYCPP_OR_MEMBER(find)
}
template<class T, class... A>
decltype(auto) py_rfind(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 1) {
        const auto pos = as_text(obj).rfind(as_text((args, ...)));
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }
    PYCPP_OR_MEMBER(rfind)
}
template<class T, class... A>
decltype(auto) py_index(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 1) {
        const auto pos = as_text(obj).find(as_text((args, ...)));
        if (pos == std::string::npos) fail("ValueError", "substring not found");
        return static_cast<int>(pos);
    } else if constexpr (is_vector_v<T> && sizeof...(A) == 1) {
        const auto& needle = (args, ...);
        for (std::size_t i = 0; i < obj.size(); ++i)
            if (obj[i] == needle) return static_cast<int>(i);
        fail("ValueError", to_repr(needle) + " is not in list");
    }
    PYCPP_OR_MEMBER(index)
}
template<class T, class... A>
decltype(auto) py_count(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 1) {
        const std::string s = as_text(obj), sub = as_text((args, ...));
        if (sub.empty()) return static_cast<int>(s.size() + 1);
        int n = 0;
        for (std::size_t pos = s.find(sub); pos != std::string::npos;
             pos = s.find(sub, pos + sub.size())) ++n;
        return n;
    } else if constexpr (is_vector_v<T> && sizeof...(A) == 1) {
        return static_cast<int>(std::count(obj.begin(), obj.end(), (args, ...)));
    }
    PYCPP_OR_MEMBER(count)
}
template<class T, class... A>
decltype(auto) py_startswith(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 1) {
        const std::string s = as_text(obj), p = as_text((args, ...));
        return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
    }
    PYCPP_OR_MEMBER(startswith)
}
template<class T, class... A>
decltype(auto) py_endswith(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 1) {
        const std::string s = as_text(obj), p = as_text((args, ...));
        return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
    }
    PYCPP_OR_MEMBER(endswith)
}

inline std::string pad_impl(const std::string& s, long long width, char fill, int where) {
    if (width <= static_cast<long long>(s.size())) return s;
    const std::size_t padding = static_cast<std::size_t>(width) - s.size();
    std::size_t left = where < 0 ? 0 : where > 0 ? padding : padding / 2 + padding % 2;
    std::string out;
    out.reserve(static_cast<std::size_t>(width));
    out.append(left, fill);
    out += s;
    out.append(padding - left, fill);
    return out;
}

#define PYCPP_PAD(name, where)                                                      \
    template<class T, class... A> decltype(auto) py_##name(T&& obj, A&&... args) {  \
        if constexpr (is_str_v<T> && (sizeof...(A) == 1 || sizeof...(A) == 2)) {    \
            auto p = std::forward_as_tuple(args...);                                \
            char fill = ' ';                                                        \
            if constexpr (sizeof...(A) == 2) {                                      \
                const std::string f = as_text(std::get<1>(p));                      \
                if (f.size() != 1) fail("TypeError", "the fill character must be exactly one character long"); \
                fill = f[0];                                                        \
            }                                                                      \
            return pad_impl(as_text(obj), static_cast<long long>(std::get<0>(p)), fill, where); \
        }                                                                          \
        PYCPP_OR_MEMBER(name)                                                      \
    }
PYCPP_PAD(center, 0)
PYCPP_PAD(ljust, -1)
PYCPP_PAD(rjust, 1)

template<class T, class... A>
decltype(auto) py_zfill(T&& obj, A&&... args) {
    if constexpr (is_str_v<T> && sizeof...(A) == 1) {
        std::string s = as_text(obj);
        const long long width = static_cast<long long>((args, ...));
        if (width <= static_cast<long long>(s.size())) return s;
        const std::size_t sign = !s.empty() && (s[0] == '-' || s[0] == '+') ? 1 : 0;
        s.insert(sign, static_cast<std::size_t>(width) - s.size(), '0');
        return s;
    }
    PYCPP_OR_MEMBER(zfill)
}

// list methods
template<class T, class... A>
decltype(auto) py_append(T&& obj, A&&... args) {
    if constexpr (is_vector_v<T> && sizeof...(A) == 1) obj.push_back((args, ...));
    PYCPP_OR_MEMBER(append)
}
template<class T, class... A>
decltype(auto) py_extend(T&& obj, A&&... args) {
    if constexpr (is_vector_v<T> && sizeof...(A) == 1) {
        auto&& source = (args, ...);
        if constexpr (std::is_same_v<plain_t<T>, plain_t<decltype(source)>>) {
            if (std::addressof(obj) == std::addressof(source)) {
                auto copy = obj;
                obj.reserve(obj.size() + copy.size());
                obj.insert(obj.end(), copy.begin(), copy.end());
            } else {
                obj.insert(obj.end(), source.begin(), source.end());
            }
        } else {
            obj.insert(obj.end(), source.begin(), source.end());
        }
    }
    PYCPP_OR_MEMBER(extend)
}
template<class T, class... A>
decltype(auto) py_insert(T&& obj, A&&... args) {
    if constexpr (is_vector_v<T> && sizeof...(A) == 2) {
        auto p = std::forward_as_tuple(args...);
        long long i = static_cast<long long>(std::get<0>(p));
        const long long n = static_cast<long long>(obj.size());
        if (i < 0) i = std::max(0LL, i + n);
        if (i > n) i = n;
        obj.insert(obj.begin() + i, std::get<1>(p));
    }
    PYCPP_OR_MEMBER(insert)
}
template<class T, class... A>
decltype(auto) py_pop(T&& obj, A&&... args) {
    if constexpr (is_vector_v<T> && sizeof...(A) <= 1) {
        if (obj.empty()) fail("IndexError", "pop from empty list");
        long long i = static_cast<long long>(obj.size()) - 1;
        if constexpr (sizeof...(A) == 1) {
            i = static_cast<long long>((args, ...));
            if (i < 0) i += static_cast<long long>(obj.size());
            if (i < 0 || i >= static_cast<long long>(obj.size()))
                fail("IndexError", "pop index out of range");
        }
        auto value = std::move(obj[static_cast<std::size_t>(i)]);
        obj.erase(obj.begin() + i);
        return value;
    } else if constexpr (is_map_v<T> && (sizeof...(A) == 1 || sizeof...(A) == 2)) {
        auto p = std::forward_as_tuple(args...);
        auto it = obj.find(std::get<0>(p));
        if (it == obj.end()) {
            if constexpr (sizeof...(A) == 2)
                return typename plain_t<T>::mapped_type(std::get<1>(p));
            else
                fail("KeyError", to_repr(std::get<0>(p)));
        }
        auto value = std::move(it->second);
        obj.erase(it);
        return value;
    }
    PYCPP_OR_MEMBER(pop)
}
template<class T, class... A>
decltype(auto) py_remove(T&& obj, A&&... args) {
    if constexpr (is_vector_v<T> && sizeof...(A) == 1) {
        auto it = std::find(obj.begin(), obj.end(), (args, ...));
        if (it == obj.end()) fail("ValueError", "list.remove(x): x not in list");
        obj.erase(it);
    } else if constexpr (is_set_v<T> && sizeof...(A) == 1) {
        if (obj.erase((args, ...)) == 0) fail("KeyError", to_repr((args, ...)));
    }
    PYCPP_OR_MEMBER(remove)
}
template<class T, class... A>
decltype(auto) py_clear(T&& obj, A&&... args) {
    if constexpr ((is_vector_v<T> || is_map_v<T> || is_set_v<T>) && sizeof...(A) == 0)
        obj.clear();
    PYCPP_OR_MEMBER(clear)
}
template<class T, class... A>
decltype(auto) py_sort(T&& obj, A&&... args) {
    if constexpr (is_vector_v<T> && sizeof...(A) == 0) std::sort(obj.begin(), obj.end());
    PYCPP_OR_MEMBER(sort)
}
template<class T, class... A>
decltype(auto) py_reverse(T&& obj, A&&... args) {
    if constexpr (is_vector_v<T> && sizeof...(A) == 0) std::reverse(obj.begin(), obj.end());
    PYCPP_OR_MEMBER(reverse)
}
template<class T, class... A>
decltype(auto) py_copy(T&& obj, A&&... args) {
    if constexpr ((is_vector_v<T> || is_map_v<T> || is_set_v<T>) && sizeof...(A) == 0)
        return plain_t<T>(obj);
    PYCPP_OR_MEMBER(copy)
}

// dict methods
template<class T, class... A>
decltype(auto) py_get(T&& obj, A&&... args) {
    if constexpr (is_map_v<T> && (sizeof...(A) == 1 || sizeof...(A) == 2)) {
        using V = typename plain_t<T>::mapped_type;
        auto p = std::forward_as_tuple(args...);
        auto it = obj.find(std::get<0>(p));
        if (it != obj.end()) return V(it->second);
        if constexpr (sizeof...(A) == 2) return V(std::get<1>(p));
        else return V{};
    }
    PYCPP_OR_MEMBER(get)
}
template<class T, class... A>
decltype(auto) py_keys(T&& obj, A&&... args) {
    if constexpr (is_map_v<T> && sizeof...(A) == 0) {
        std::vector<typename plain_t<T>::key_type> out;
        out.reserve(obj.size());
        for (const auto& kv : obj) out.push_back(kv.first);
        return out;
    }
    PYCPP_OR_MEMBER(keys)
}
template<class T, class... A>
decltype(auto) py_values(T&& obj, A&&... args) {
    if constexpr (is_map_v<T> && sizeof...(A) == 0) {
        std::vector<typename plain_t<T>::mapped_type> out;
        out.reserve(obj.size());
        for (const auto& kv : obj) out.push_back(kv.second);
        return out;
    }
    PYCPP_OR_MEMBER(values)
}
template<class T, class... A>
decltype(auto) py_items(T&& obj, A&&... args) {
    if constexpr (is_map_v<T> && sizeof...(A) == 0) {
        using Pair = std::pair<typename plain_t<T>::key_type, typename plain_t<T>::mapped_type>;
        std::vector<Pair> out;
        out.reserve(obj.size());
        for (const auto& kv : obj) out.emplace_back(kv.first, kv.second);
        return out;
    }
    PYCPP_OR_MEMBER(items)
}
template<class T, class... A>
decltype(auto) py_update(T&& obj, A&&... args) {
    if constexpr (is_map_v<T> && sizeof...(A) == 1) {
        for (const auto& kv : (args, ...)) obj.insert_or_assign(kv.first, kv.second);
    }
    PYCPP_OR_MEMBER(update)
}
template<class T, class... A>
decltype(auto) py_setdefault(T&& obj, A&&... args) {
    if constexpr (is_map_v<T> && sizeof...(A) == 2) {
        auto p = std::forward_as_tuple(args...);
        return obj.try_emplace(std::get<0>(p), std::get<1>(p)).first->second;
    }
    PYCPP_OR_MEMBER(setdefault)
}

// set methods
template<class T, class... A>
decltype(auto) py_add(T&& obj, A&&... args) {
    if constexpr (is_set_v<T> && sizeof...(A) == 1) obj.insert((args, ...));
    PYCPP_OR_MEMBER(add)
}
template<class T, class... A>
decltype(auto) py_discard(T&& obj, A&&... args) {
    if constexpr (is_set_v<T> && sizeof...(A) == 1) obj.erase((args, ...));
    PYCPP_OR_MEMBER(discard)
}

#undef PYCPP_OR_MEMBER
#undef PYCPP_STR_TRANSFORM
#undef PYCPP_STR_TEST
#undef PYCPP_STRIP
#undef PYCPP_PAD

// -----------------------------------------------------------------------------
// Format specifications
// -----------------------------------------------------------------------------

struct FormatSpec {
    char fill = ' ', align = 0, sign = '-', type = 0, grouping = 0;
    bool alternate = false;
    long long width = -1, precision = -1;
};

inline FormatSpec parse_format_spec(const std::string& spec) {
    FormatSpec f;
    std::size_t i = 0;
    const auto is_align = [](char c) { return c == '<' || c == '>' || c == '^' || c == '='; };
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
        while (i < spec.size() && std::isdigit(static_cast<unsigned char>(spec[i]))) {
            const int digit = spec[i++] - '0';
            if (f.width > (std::numeric_limits<long long>::max() - digit) / 10)
                fail("ValueError", "format width too large");
            f.width = f.width * 10 + digit;
        }
    }
    if (i < spec.size() && (spec[i] == ',' || spec[i] == '_')) f.grouping = spec[i++];
    if (i < spec.size() && spec[i] == '.') {
        ++i;
        f.precision = 0;
        if (i >= spec.size() || !std::isdigit(static_cast<unsigned char>(spec[i])))
            fail("ValueError", "format specifier missing precision");
        while (i < spec.size() && std::isdigit(static_cast<unsigned char>(spec[i]))) {
            const int digit = spec[i++] - '0';
            if (f.precision > (std::numeric_limits<long long>::max() - digit) / 10)
                fail("ValueError", "format precision too large");
            f.precision = f.precision * 10 + digit;
        }
    }
    if (i < spec.size()) f.type = spec[i++];
    if (i != spec.size()) fail("ValueError", "invalid format specifier '" + spec + "'");
    return f;
}

inline std::string group_digits(const std::string& s, char separator, std::size_t every = 3) {
    const auto end = s.find_first_of(".eE");
    const std::size_t n = end == std::string::npos ? s.size() : end;
    std::string out;
    out.reserve(s.size() + n / every);
    for (std::size_t i = 0; i < n; ++i) {
        if (i && (n - i) % every == 0) out.push_back(separator);
        out.push_back(s[i]);
    }
    if (end != std::string::npos) out.append(s, end, std::string::npos);
    return out;
}

inline std::string pad_formatted(const FormatSpec& f, const std::string& sign,
        const std::string& body, char default_align) {
    const std::size_t size = sign.size() + body.size();
    if (f.width < 0 || static_cast<unsigned long long>(f.width) <= size) return sign + body;
    const std::size_t padding = static_cast<std::size_t>(f.width) - size;
    std::string out;
    out.reserve(static_cast<std::size_t>(f.width));
    switch (f.align ? f.align : default_align) {
        case '<': out = sign + body; out.append(padding, f.fill); break;
        case '^': out.append(padding / 2, f.fill); out += sign; out += body;
                  out.append(padding - padding / 2, f.fill); break;
        case '=': out = sign; out.append(padding, f.fill); out += body; break;
        default: out.append(padding, f.fill); out += sign; out += body; break;
    }
    return out;
}

inline std::string format_float(double value, const FormatSpec& f) {
    std::string sign = std::signbit(value) && !std::isnan(value) ? "-"
        : f.sign == '+' ? "+" : f.sign == ' ' ? " " : "";
    value = std::fabs(value);
    char type = f.type;
    std::string body;
    if (std::isinf(value) || std::isnan(value)) {
        body = std::isnan(value) ? "nan" : "inf";
    } else if (type == 0 && f.precision < 0) {
        body = float_to_str(value);
    } else {
        const long long precision = f.precision < 0 ? 6 : f.precision;
        bool percent = false;
        if (type == '%') { value *= 100; type = 'f'; percent = true; }
        if (type == 0) type = 'g';
        if (type != 'f' && type != 'F' && type != 'e' && type != 'E' &&
            type != 'g' && type != 'G')
            fail("ValueError", std::string("unknown format code '") + f.type + "' for a float");

        if (precision > 300) fail("ValueError", "precision too large");
        char fmt[32];
        std::snprintf(fmt, sizeof(fmt), "%%%s.%lld%c", f.alternate ? "#" : "", precision, type);
        char buffer[1024];
        const int n = std::snprintf(buffer, sizeof(buffer), fmt, value);
        if (n < 0 || static_cast<std::size_t>(n) >= sizeof(buffer))
            fail("ValueError", "formatted float too large");
        body.assign(buffer, static_cast<std::size_t>(n));
        if (percent) body.push_back('%');
    }
    if (f.grouping) body = group_digits(body, f.grouping);
    return pad_formatted(f, sign, body, '>');
}

template<class T>
std::string format_spec(const T& value, const std::string& spec) {
    if (spec.empty()) return to_str(value);
    const FormatSpec f = parse_format_spec(spec);
    if constexpr (std::is_same_v<plain_t<T>, bool>) {
        if (f.type == 0 || f.type == 's') return pad_formatted(f, "", to_str(value), '<');
        return format_spec(static_cast<long long>(value), spec);
    } else if constexpr (std::is_integral_v<T> && !std::is_same_v<plain_t<T>, char>) {
        if (f.type == 'e' || f.type == 'E' || f.type == 'f' || f.type == 'F' ||
            f.type == 'g' || f.type == 'G' || f.type == '%')
            return format_float(static_cast<double>(value), f);
        if (f.precision >= 0) fail("ValueError", "precision not allowed in integer format specifier");
        const bool negative = std::is_signed_v<T> && value < 0;
        using U = std::make_unsigned_t<plain_t<T>>;
        U number = static_cast<U>(value);
        if (negative) number = U(0) - number;
        unsigned base = 10;
        const char* digits = "0123456789abcdef";
        std::string prefix;
        switch (f.type) {
            case 0: case 'd': case 'n': break;
            case 'b': base = 2; prefix = "0b"; break;
            case 'o': base = 8; prefix = "0o"; break;
            case 'x': base = 16; prefix = "0x"; break;
            case 'X': base = 16; prefix = "0X"; digits = "0123456789ABCDEF"; break;
            case 'c':
                if (number > 0x10ffff) fail("OverflowError", "%c arg not in range(0x110000)");
                return pad_formatted(f, "", std::string(1, static_cast<char>(number)), '<');
            default:
                fail("ValueError", std::string("unknown format code '") + f.type + "' for an integer");
        }
        char buffer[std::numeric_limits<U>::digits + 1];
        char* end = buffer + sizeof(buffer);
        char* p = end;
        do { *--p = digits[number % base]; number /= base; } while (number);
        std::string body(p, end);
        if (f.grouping) body = group_digits(body, f.grouping, base == 10 ? 3 : 4);
        std::string sign = negative ? "-" : f.sign == '+' ? "+" : f.sign == ' ' ? " " : "";
        if (f.alternate) sign += prefix;
        return pad_formatted(f, sign, body, '>');
    } else if constexpr (std::is_floating_point_v<T>) {
        return format_float(static_cast<double>(value), f);
    } else {
        if (f.type != 0 && f.type != 's')
            fail("ValueError", std::string("unknown format code '") + f.type + "' for a string");
        std::string text = as_text(value);
        if (f.precision >= 0 && text.size() > static_cast<std::size_t>(f.precision))
            text.resize(static_cast<std::size_t>(f.precision));
        return pad_formatted(f, "", text, '<');
    }
}

// -----------------------------------------------------------------------------
// type(), isinstance(), any(), all(), sorting
// -----------------------------------------------------------------------------

struct TypeObject {
    std::string __name__;
    bool operator==(const TypeObject& other) const { return __name__ == other.__name__; }
    bool operator!=(const TypeObject& other) const { return !(*this == other); }
    friend std::ostream& operator<<(std::ostream& os, const TypeObject& type) {
        return os << "<class '" << type.__name__ << "'>";
    }
};

inline std::string demangle_type(const char* raw) {
    std::string result = raw;
#if defined(__GNUG__)
    int status = 0;
    char* demangled = abi::__cxa_demangle(raw, nullptr, nullptr, &status);
    if (status == 0 && demangled) result = demangled;
    std::free(demangled);
#else
    for (const char* prefix : {"class ", "struct "}) {
        std::size_t pos;
        while ((pos = result.find(prefix)) != std::string::npos)
            result.erase(pos, std::strlen(prefix));
    }
#endif
    const auto angle = result.find('<');
    const auto scope = result.rfind("::", angle);
    if (scope != std::string::npos) result.erase(0, scope + 2);
    return result;
}

template<class T>
TypeObject type_of(const T& value) {
    using P = plain_t<T>;
    if constexpr (std::is_same_v<P, bool>) return {"bool"};
    else if constexpr (is_text<P> || std::is_same_v<P, char>) return {"str"};
    else if constexpr (std::is_integral_v<P>) return {"int"};
    else if constexpr (std::is_floating_point_v<P>) return {"float"};
    else if constexpr (is_tuple_v<P>) return {"tuple"};
    else if constexpr (is_map_v<P>) return {"dict"};
    else if constexpr (is_set_v<P>) return {"set"};
    else if constexpr (requires { value.begin(); value.end(); }) return {"list"};
    else return {demangle_type(typeid(value).name())};
}

template<class C> bool any(const C& values) {
    for (const auto& value : values) if (value) return true;
    return false;
}
template<class C> bool all(const C& values) {
    for (const auto& value : values) if (!value) return false;
    return true;
}

template<class V>
bool isinst_int(const V&) {
    using P = plain_t<V>;
    return std::is_integral_v<P> && !std::is_same_v<P, bool> && !std::is_same_v<P, char>;
}
template<class V> bool isinst_float(const V&) { return std::is_floating_point_v<plain_t<V>>; }
template<class V> bool isinst_bool(const V&) { return std::is_same_v<plain_t<V>, bool>; }
template<class V> bool isinst_str(const V&) { return is_str_v<V>; }
template<class V> bool isinst_list(const V&) { return is_vector_v<V>; }
template<class V> bool isinst_dict(const V&) { return is_map_v<V>; }
template<class V> bool isinst_set(const V&) { return is_set_v<V>; }
template<class V> bool isinst_tuple(const V&) { return is_tuple_v<V>; }

template<class V, class K>
void sort_by(V& values, K&& key, bool reverse) {
    std::stable_sort(values.begin(), values.end(),
        [&](const auto& a, const auto& b) {
            return reverse ? key(b) < key(a) : key(a) < key(b);
        });
}

template<class T, class V>
bool is_instance(const V& value) {
    if constexpr (std::is_pointer_v<V>) {
        using B = std::remove_cv_t<std::remove_pointer_t<V>>;
        if constexpr (std::is_class_v<B> && std::is_polymorphic_v<B>)
            return value != nullptr && dynamic_cast<const T*>(value) != nullptr;
        else
            return std::is_base_of_v<T, B> || std::is_same_v<T, B>;
    } else if constexpr (std::is_class_v<V> && std::is_polymorphic_v<V>) {
        return dynamic_cast<const T*>(&value) != nullptr;
    } else {
        return std::is_base_of_v<T, V> || std::is_same_v<T, V>;
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

#endif // PYCPP_RUNTIME_H
)PYCPP";

inline void writeRuntimeHeader(std::filesystem::path dir) {
    if (dir.empty()) dir = ".";
    std::ofstream(dir / "pycpp_runtime.h", std::ios::binary) << kPycppRuntime;
}
#endif
