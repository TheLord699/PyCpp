#!/usr/bin/env python3
"""
fix3.py -- print(100.0) showed "1e+02"; floats now print like Python (100.0, 0.1, 1e-05, 1e+16).
Run AFTER fix.py and fix2.py, from the repo root:
    python fix3.py            # apply
    python fix3.py --check    # dry run
"""
import re
import sys
from pathlib import Path

CHECK = "--check" in sys.argv
RT = Path("PyCpp/include/pycpp_runtime_text.h")


def rx(s):
    return re.compile(r"\s+".join(re.escape(t) for t in s.split()))


FLOAT_FN = r'''// Shortest text that reads back as the same value, in Python's style:
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

'''

OLD_BLOCK = r'''char buf[48];
        for (int p = 1; p <= 17; ++p) {                // shortest text that round-trips
            std::snprintf(buf, sizeof buf, "%.*g", p, static_cast<double>(v));
            if (static_cast<T>(std::strtod(buf, nullptr)) == v) break;
        }
        std::string s = buf;
        if (s.find_first_of(".en") == std::string::npos) s += ".0";
        return s;'''

FWD = r'''template <class T> std::string to_str(const T& v);'''


def main():
    if not RT.exists():
        print(f"[MISSING] {RT}")
        sys.exit(1)
    raw = RT.read_bytes().decode("utf-8")
    crlf = "\r\n" in raw
    text = raw.replace("\r\n", "\n")

    if "float_to_str" in text:
        print("[SKIP] already applied")
        return
    if "container_str" not in text:
        print("fix2.py has not been applied yet. Run fix.py and fix2.py first.")
        sys.exit(1)

    for name, anchor in (("float block", OLD_BLOCK), ("forward declaration", FWD)):
        n = len(list(rx(anchor).finditer(text)))
        if n != 1:
            print(f"[FAIL] {name}: anchor found {n} times (need exactly 1)")
            sys.exit(1)

    m = rx(OLD_BLOCK).search(text)
    text = text[:m.start()] + "return float_to_str(v);" + text[m.end():]
    m = rx(FWD).search(text)
    text = text[:m.start()] + FLOAT_FN + text[m.start():]
    print("[OK  ] pycpp_runtime_text.h: float formatting")

    if CHECK:
        print("\nDry run OK.")
        return
    RT.write_bytes((text.replace("\n", "\r\n") if crlf else text).encode("utf-8"))
    print("\nPatched 1 file.")


if __name__ == "__main__":
    main()