#!/usr/bin/env python3
"""
fix2.py -- second patch: the remaining known gaps.
Run AFTER fix.py, from the repo root:
    python fix2.py            # apply
    python fix2.py --check    # dry run

Fixes
  * `/` is true division like Python (7 / 2 == 3.5); `//` floors (also for
    negatives and floats); `%` takes the sign of the divisor and works on floats;
    `**` on ints stays an int. Division/modulo by zero stops with ZeroDivisionError.
  * `x //= y` was emitted as C++ `x //= y`, which is a COMMENT. `//=`, `%=` and
    `**=` (invalid C++) are now rewritten correctly.
  * print() writes Python-style text: True/False, shortest round-trip floats
    (3.0, 0.1), and [1, 2] / {'a': 1} for list/dict/set. str(x) does the same.
  * list(), dict(), set(), tuple() calls never parsed ("Expected expression").
    They parse now. dict() / list() / set() with no arguments emit `{}` (typed by
    the declaration) instead of the invalid std::map<auto, auto>().
  * Two modules with the same file name now give a clear error instead of
    silently overwriting each other's generated files.
  * The compiled program's exit code is now the exit code of pycpp itself, and a
    crash (e.g. a failed assert -> SIGABRT) is reported as a normal non-zero exit
    instead of "Could not start the compiled program".
"""
import re
import sys
from pathlib import Path

ROOT = Path(".")
CHECK = "--check" in sys.argv
EDITS = []


def edit(path, mode, anchor, new, end=None):
    EDITS.append((path, mode, anchor, new, end))


def rx(s):
    """Whitespace-insensitive regex for a (possibly multi-line) snippet."""
    return re.compile(r"\s+".join(re.escape(t) for t in s.split()))


CG = "PyCpp/src/code_generator.cpp"
SA = "PyCpp/src/semantic_analyzer.cpp"
PAR = "PyCpp/src/pycpp_parser.cpp"
MAIN = "PyCpp/src/main.cpp"
BACKEND = "PyCpp/src/compiler_backend.cpp"
RT = "PyCpp/include/pycpp_runtime_text.h"
README = "README.md"

# ============================================================ runtime header
edit(RT, "replace", r'''#include <cctype>''', r'''#include <cctype>
#include <cmath>''')

# forward declarations so to_str can print containers recursively
edit(RT, "before", r'''template <class T>
std::string to_str(const T& v) {''',
     r'''template <class T> std::string to_str(const T& v);
template <class T> std::string to_repr(const T& v);
template <class C> std::string container_str(const C& c);

''')

# containers inside to_str
edit(RT, "replace", r'''std::ostringstream o; o << v; return o.str();''',
     r'''if constexpr (requires { v.begin(); v.end(); }) return container_str(v);
        else { std::ostringstream o; o << v; return o.str(); }''')

# container printing + Python arithmetic
edit(RT, "before", r'''// bin()/oct()/hex(): Python-style text''',
     r'''// Text of an element inside a container: strings are quoted, like Python's repr()
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

''')

# ============================================================ code_generator.cpp
# print(): Python-style text for every argument
edit(CG, "replace", r'''output << " << ";
                generateExpression(call_expr->arguments[i].get());''',
     r'''useRuntime();
                output << " << pycpp::to_str(";
                generateExpression(call_expr->arguments[i].get());
                output << ")";''')

# / // % **  -> runtime helpers
edit(CG, "before", r'''// Handle power operator''',
     r'''if (bin_op->op == "/" || bin_op->op == "//" || bin_op->op == "%" || bin_op->op == "**") {
            useRuntime();
            output << (bin_op->op == "/"  ? "pycpp::truediv("
                     : bin_op->op == "//" ? "pycpp::floordiv("
                     : bin_op->op == "%"  ? "pycpp::mod(" : "pycpp::power(");
            generateExpression(bin_op->left.get());
            output << ", ";
            generateExpression(bin_op->right.get());
            output << ")";
            return;
        }

        ''')

# //=  %=  **=
edit(CG, "before", r'''// Handle compound assignments''',
     r'''if (bin_op->op == "//=" || bin_op->op == "%=" || bin_op->op == "**=") {
            // `x //= y` must not be emitted as-is: in C++ that starts a comment
            useRuntime();
            generateExpression(bin_op->left.get());
            output << " = "
                   << (bin_op->op == "//=" ? "pycpp::floordiv("
                       : bin_op->op == "%=" ? "pycpp::mod(" : "pycpp::power(");
            generateExpression(bin_op->left.get());
            output << ", ";
            generateExpression(bin_op->right.get());
            output << ")";
            return;
        }

        ''')

# dict() / list() / set() without arguments, dict(x)
edit(CG, "before", r'''else if (function_name == "list") {''',
     r'''else if ((function_name == "dict" || function_name == "list" || function_name == "set") &&
                 call_expr->arguments.empty()) {
            output << "{}";   // takes its type from the declaration it initialises
            return;
        }
        else if (function_name == "dict" && call_expr->arguments.size() == 1) {
            output << lambdaCapture() << "() { auto&& pycpp_c = ";
            generateExpression(call_expr->arguments[0].get());
            output << "; return std::decay_t<decltype(pycpp_c)>(pycpp_c); }()";
            return;
        }
        ''')

# ============================================================ pycpp_parser.cpp
# list(...), dict(...), set(...), tuple(...) as calls
edit(PAR, "replace", r'''check(TokenType::STR) || check(TokenType::BOOL)) {''',
     r'''check(TokenType::STR) || check(TokenType::BOOL) ||
                 check(TokenType::LIST) || check(TokenType::DICT) ||
                 check(TokenType::SET) || check(TokenType::TUPLE)) {''')

# ============================================================ semantic_analyzer.cpp
edit(SA, "after", r'''addSymbol("nullptr", "void*");''',
     r'''
    for (const char* name : { "list", "dict", "set", "tuple" }) {
        addSymbol(name, "function");
    }''')

# ============================================================ main.cpp
# same module name in two folders
edit(MAIN, "after",
     r'''std::vector<std::pair<std::string, std::unique_ptr<Program>>> modules;''',
     r'''
    {
        // Imports are by name, and generated files are named after the module, so two
        // different files with the same name cannot coexist.
        std::map<std::string, std::string> stems;
        for (const auto& file : files) {
            const std::string stem = std::filesystem::path(file).stem().string();
            auto inserted = stems.emplace(stem, file);
            if (!inserted.second && inserted.first->second != file)
                throw std::runtime_error("modules '" + inserted.first->second + "' and '" + file +
                    "' share the name '" + stem + "'; imports are by name, so rename one of them");
        }
    }''')

# exit code of the compiled program
edit(MAIN, "before",
     r'''// Compile the generated C++ with the selected compiler, then run the result.''',
     r'''// Exit code of the program that was built and run; becomes pycpp's own exit code.
static int g_programExitCode = 0;

''')
edit(MAIN, "replace", r'''TerminalFormatter::printExitCode(exitCode);''',
     r'''TerminalFormatter::printExitCode(exitCode);
    g_programExitCode = exitCode;''')
edit(MAIN, "replace", r'''return success ? 0 : 1;''', r'''return success ? g_programExitCode : 1;''')

# ============================================================ compiler_backend.cpp
# a crashed program (abort, segfault) is not "could not start"
edit(BACKEND, "replace", r'''if (WIFEXITED(status)) return WEXITSTATUS(status);''',
     r'''if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);''')

# ============================================================ README
edit(README, "after", r'''`bin()`, `oct()` and `hex()` return Python-style strings such as `"0b101"`.''',
     r'''
- Python arithmetic: `/` is true division (`7 / 2` is `3.5`), `//` floors (`-7 // 2` is `-4`), `%` takes the sign of the divisor, and `**` on integers stays an integer. Dividing by zero stops the program with `ZeroDivisionError`.
- `print()` writes Python-style text: `True` / `False`, floats such as `3.0` and `0.1`, and `[1, 2]` / `{'a': 1}` for containers.
- `list()`, `dict()` and `set()` create empty collections, typed by the declaration: `d: dict[str, int] = dict()`.
- `pycpp` exits with the exit code of the program it ran.''')

# ============================================================ example
edit("PyCpp/examples/simple/arithmetic.pycpp", "create", None,
     '''def main() -> int:
    print(7 / 2)
    print(7 // 2)
    print(-7 // 2)
    print(-7 % 3)
    print(2 ** 10)

    x: int = 17
    x //= 5
    print(x)

    print(True)
    print(1 == 2)
    print(3.0)

    nums: list[int] = [1, 2, 3]
    print(nums)

    names: dict[str, int] = {"a": 1, "b": 2}
    print(names)

    empty: dict[str, int] = dict()
    print(len(empty))

    return 0
''')


# ======================================================================= engine
def apply_edit(text, mode, anchor, new, end):
    if mode == "replace_all":
        pat = rx(anchor)
        if not pat.search(text):
            return text, "skip"
        return pat.sub(lambda m: new, text), "ok"

    if new.strip() and mode in ("before", "after", "replace") and rx(new.strip()).search(text):
        return text, "skip"

    found = list(rx(anchor).finditer(text))
    if len(found) != 1:
        raise ValueError(f"anchor found {len(found)} times (need exactly 1):\n---\n{anchor[:200]}\n---")
    m = found[0]

    if mode == "before":
        return text[:m.start()] + new + text[m.start():], "ok"
    if mode == "after":
        return text[:m.end()] + new + text[m.end():], "ok"
    if mode == "replace":
        return text[:m.start()] + new + text[m.end():], "ok"
    raise ValueError("unknown mode " + mode)


def main():
    rt = ROOT / RT
    if rt.exists() and "to_base" not in rt.read_text(encoding="utf-8"):
        print("fix.py has not been applied yet. Run `python fix.py` first, then re-run fix2.py.")
        sys.exit(1)

    by_file = {}
    for e in EDITS:
        by_file.setdefault(e[0], []).append(e)

    results = {}
    failed = False
    for path, edits in by_file.items():
        p = ROOT / path

        if edits[0][1] == "create":
            if p.exists():
                print(f"[SKIP] {path}: already exists")
            else:
                print(f"[OK  ] {path}: create")
                results[p] = edits[0][3]
            continue

        if not p.exists():
            print(f"[MISSING] {path}")
            failed = True
            continue
        raw = p.read_bytes().decode("utf-8")
        crlf = "\r\n" in raw
        text = raw.replace("\r\n", "\n")
        for (_, mode, anchor, new, end) in edits:
            try:
                text, status = apply_edit(text, mode, anchor, new, end)
            except ValueError as ex:
                print(f"[FAIL] {path}: {ex}")
                failed = True
                break
            print(f"[{status.upper():4}] {path}: {mode}")
        else:
            results[p] = text.replace("\n", "\r\n") if crlf else text

    if failed:
        print("\nNothing was written. Fix the reported anchor(s) and re-run.")
        sys.exit(1)

    if CHECK:
        print("\nDry run OK: every edit applies cleanly.")
        return
    for p, text in results.items():
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(text.encode("utf-8"))
    print(f"\nPatched {len(results)} files.")


if __name__ == "__main__":
    main()