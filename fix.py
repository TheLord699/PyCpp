#!/usr/bin/env python3
"""
fix.py - applies a batch of cleanups and small features to the PyCPP repo.

Run it from the repository root (the folder that contains PyCpp/ and CMakeLists.txt):

    python fix.py              apply everything
    python fix.py --dry-run    show what would change, write nothing
    python fix.py --test       apply, then run PyCpp/tests/run_tests.py
    python fix.py --root DIR   use DIR as the repo root

Properties:
  * Every change is a "group". A group is applied only if ALL of its edits match
    exactly once; otherwise nothing in that group is touched and the reason is printed.
  * Re-running is safe: a group whose result is already present is skipped.
  * CRLF/LF line endings and a UTF-8 BOM are preserved.
  * Originals of changed files are copied to .fix_backup/ (add it to .gitignore, or
    just use `git diff` / `git checkout` instead).

Groups
  1  Remove the unused HeaderGenerator (files, CMake entry, includes)
  2  Remove unreachable bin()/oct()/hex() duplicates in the code generator
  3  Remove unreachable '**' and '//' blocks in the code generator
  4  ':=' gives a clear "not supported" error instead of half-working walrus code
  5  Dict indexing: missing key raises KeyError; `d[k] = v` still inserts
  6  Set literals: {1, 2, 3}
  7  __str__ makes print(obj), str(obj) and f"{obj}" work
  8  GitHub Actions CI (Linux gcc, macOS clang, Windows MSVC)
  9  README notes
"""
import argparse
import subprocess
import sys
from pathlib import Path

# --------------------------------------------------------------------------------------
# Group definitions
# --------------------------------------------------------------------------------------

CODEGEN = "PyCpp/src/code_generator.cpp"
CODEGEN_H = "PyCpp/include/code_generator.h"
PARSER = "PyCpp/src/pycpp_parser.cpp"
RUNTIME = "PyCpp/include/pycpp_runtime_text.h"
MAIN = "PyCpp/src/main.cpp"
CMAKE = "CMakeLists.txt"
README = "README.md"

GROUPS = []


def group(name, done, ops):
    GROUPS.append({"name": name, "done": done, "ops": ops})


def has(rel, needle):
    def check(root):
        p = root / rel
        if not p.exists():
            return False
        return needle in read(p)[0]
    return check


def lacks(rel, needle):
    def check(root):
        p = root / rel
        return p.exists() and needle not in read(p)[0]
    return check


# ---- 1. remove HeaderGenerator --------------------------------------------------------
group(
    "1  remove unused HeaderGenerator",
    lambda root: not (root / "PyCpp/src/header_generator.cpp").exists(),
    [
        ("edit", CMAKE, '    "PyCpp/src/header_generator.cpp"\n', ""),
        ("edit", MAIN, '#include "../include/header_generator.h"\n', ""),
        ("edit", CODEGEN, '#include "../include/header_generator.h"\n', ""),
        ("delete", "PyCpp/src/header_generator.cpp"),
        ("delete", "PyCpp/include/header_generator.h"),
    ],
)

# ---- 2. dead bin/oct/hex ---------------------------------------------------------------
group(
    "2  remove unreachable bin/oct/hex branches",
    lacks(CODEGEN, '"\\"0b0\\""'),
    [
        (
            "edit",
            CODEGEN,
            r'''        else if (function_name == "bin") {
            if (call_expr->arguments.size() > 0) {
                output << lambdaCapture() << "() { std::stringstream ss; ss << std::hex << ";
                generateExpression(call_expr->arguments[0].get());
                output << "; return ss.str(); }()";
            }
            else {
                output << "\"0b0\"";
            }
            return;
        }
        else if (function_name == "oct") {
            if (call_expr->arguments.size() > 0) {
                output << lambdaCapture() << "() { std::stringstream ss; ss << std::oct << ";
                generateExpression(call_expr->arguments[0].get());
                output << "; return ss.str(); }()";
            }
            else {
                output << "\"0o0\"";
            }
            return;
        }
        else if (function_name == "hex") {
            if (call_expr->arguments.size() > 0) {
                output << lambdaCapture() << "() { std::stringstream ss; ss << std::hex << ";
                generateExpression(call_expr->arguments[0].get());
                output << "; return ss.str(); }()";
            }
            else {
                output << "\"0x0\"";
            }
            return;
        }
''',
            "",
        ),
    ],
)

# ---- 3. dead ** and // -----------------------------------------------------------------
group(
    "3  remove unreachable '**' and '//' blocks",
    lacks(CODEGEN, "// Handle power operator"),
    [
        (
            "edit",
            CODEGEN,
            r'''        // Handle power operator
        if (bin_op->op == "**") {
            output << "std::pow(";
            generateExpression(bin_op->left.get());
            output << ", ";
            generateExpression(bin_op->right.get());
            output << ")";
            return;
        }

        // Handle floor division
        if (bin_op->op == "//") {
            output << "static_cast<int>(";
            generateExpression(bin_op->left.get());
            output << " / ";
            generateExpression(bin_op->right.get());
            output << ")";
            return;
        }

''',
            "",
        ),
    ],
)

# ---- 4. walrus -------------------------------------------------------------------------
group(
    "4  ':=' reports a clear error",
    lacks(PARSER, "return parseWalrusOperator();"),
    [
        (
            "edit",
            PARSER,
            r'''    // Handle walrus operator first
    if (check(TokenType::IDENTIFIER) && checkNext(TokenType::COLON) &&
        position + 2 < tokens.size() && tokens[position + 2].type == TokenType::EQUAL) {
        return parseWalrusOperator();
    }

    // lambda has the lowest precedence of all expressions
''',
            "    // lambda has the lowest precedence of all expressions\n",
        ),
        (
            "edit",
            PARSER,
            "    // Conditional expression:  value_if_true if condition else value_if_false\n",
            r'''    if (check(TokenType::WALRUS)) {
        throw std::runtime_error(withHint("assignment expressions (':=') are not supported yet by PyCPP",
                                 "assign on its own line first (`x = value`), then use x"));
    }

    // Conditional expression:  value_if_true if condition else value_if_false
''',
        ),
        (
            "create",
            "PyCpp/tests/errors/walrus_not_supported.pycpp",
            """# expect-exit: 1
# expect-stderr: error[E0011]: assignment expressions (':=') are not supported yet by PyCPP
def main() -> int:
    x = 1
    y = x := 2
    return 0
""",
        ),
    ],
)

# ---- 5. dict KeyError ------------------------------------------------------------------
group(
    "5  dict indexing raises KeyError (assignment still inserts)",
    has(RUNTIME, "at_w"),
    [
        (
            "edit",
            RUNTIME,
            r'''// a[i]: negative indices count from the end, an index out of range raises IndexError
template <class C, class I>
decltype(auto) at(C&& c, I i) {
    using Plain = std::remove_cv_t<std::remove_reference_t<C>>;
    constexpr bool direct = std::is_pointer_v<Plain> || std::is_array_v<Plain> ||
                            !std::is_integral_v<I> || requires { typename Plain::mapped_type; };
    if constexpr (direct) {
        return (c[i]);
    } else {''',
            r'''// a[i]: negative indices count from the end, an index out of range raises IndexError.
// For a dict, a missing key raises KeyError (`d[k] = v` goes through at_w and inserts).
template <class C, class I>
decltype(auto) at(C&& c, I i) {
    using Plain = std::remove_cv_t<std::remove_reference_t<C>>;
    constexpr bool is_dict = requires { typename Plain::mapped_type; };
    constexpr bool direct = std::is_pointer_v<Plain> || std::is_array_v<Plain> ||
                            !std::is_integral_v<I>;
    if constexpr (is_dict) {
        auto it = c.find(i);
        if (it == c.end()) fail("KeyError", to_repr(i));
        return (it->second);
    } else if constexpr (direct) {
        return (c[i]);
    } else {''',
        ),
        (
            "edit",
            RUNTIME,
            "// ===================== Python methods of str, list, dict and set =====================\n",
            r'''// `d[k] = v`: a dict inserts the key when it is missing, as in Python.
// Everything else behaves like at().
template <class C, class I>
decltype(auto) at_w(C&& c, I i) {
    using Plain = std::remove_cv_t<std::remove_reference_t<C>>;
    if constexpr (requires { typename Plain::mapped_type; }) return (c[i]);
    else return at(std::forward<C>(c), i);
}

// ===================== Python methods of str, list, dict and set =====================
''',
        ),
        (
            "edit",
            CODEGEN_H,
            "    bool class_is_polymorphic = false;\n",
            "    bool class_is_polymorphic = false;\n"
            "    bool subscript_write = false;   // the subscript being generated is the target of a plain '='\n",
        ),
        (
            "edit",
            CODEGEN,
            r'''            // [fix2] bounds-checked, Python-style indexing: a[-1] works, a[99] raises IndexError
            useRuntime();
            output << "pycpp::at(";
''',
            r'''            // [fix2] bounds-checked, Python-style indexing: a[-1] works, a[99] raises IndexError
            useRuntime();
            const bool for_write = subscript_write;   // only the outermost subscript of `x[i] = v`
            subscript_write = false;
            output << (for_write ? "pycpp::at_w(" : "pycpp::at(");
''',
        ),
        (
            "edit",
            CODEGEN,
            r'''            if (needs_parens) output << "(";
            generateExpression(bin_op->left.get());
''',
            r'''            if (needs_parens) output << "(";
            if (auto sub = dynamic_cast<CallExpr*>(bin_op->left.get()); sub && sub->is_subscript)
                subscript_write = true;   // d[k] = v inserts into a dict
            generateExpression(bin_op->left.get());
''',
        ),
        (
            "create",
            "PyCpp/tests/regressions/dict_missing_key_raises.pycpp",
            """# expect-exit: 1
# expect-stdout: 1
# expect-stderr: KeyError: 'zz'
def main() -> int:
    d: dict[str, int] = {"a": 1}
    print(d["a"])
    print(d["zz"])
    return 0
""",
        ),
        (
            "create",
            "PyCpp/tests/regressions/dict_assignment_inserts.pycpp",
            """# expect-exit: 0
# expect-stdout: 1 5 2
def main() -> int:
    d: dict[str, int] = {"a": 1}
    d["b"] = 5
    print(d["a"], d["b"], len(d))
    return 0
""",
        ),
        (
            "create",
            "PyCpp/tests/regressions/dict_missing_key_is_catchable.pycpp",
            """# expect-exit: 0
# expect-stdout: caught
def main() -> int:
    d: dict[str, int] = {"a": 1}
    try:
        print(d["nope"])
    except KeyError:
        print("caught")
    return 0
""",
        ),
    ],
)

# ---- 6. set literals -------------------------------------------------------------------
group(
    "6  set literals {1, 2, 3}",
    has(PARSER, "set literal"),
    [
        (
            "edit",
            PARSER,
            r'''            consume(TokenType::RBRACE, "Expected '}' after the set comprehension");
            return comp;
        }
        consume(TokenType::COLON, "Expected ':' after dictionary key");
        auto first_value = parseExpression();
''',
            r'''            consume(TokenType::RBRACE, "Expected '}' after the set comprehension");
            return comp;
        }
        if (check(TokenType::COMMA) || check(TokenType::RBRACE)) {   // {1, 2, 3}: set literal
            auto elements = std::make_unique<ListLiteral>();
            elements->line = dict->line;
            elements->column = dict->column;
            elements->elements.push_back(std::move(first_key));
            while (match(TokenType::COMMA)) {
                if (check(TokenType::RBRACE)) break;   // trailing comma
                elements->elements.push_back(parseExpression());
            }
            consume(TokenType::RBRACE, "Expected '}' after set elements");
            auto callee = std::make_unique<Identifier>();
            callee->name = "set";
            callee->line = dict->line;
            callee->column = dict->column;
            auto call = std::make_unique<CallExpr>();
            call->line = dict->line;
            call->column = dict->column;
            call->callee = std::move(callee);
            call->arguments.push_back(std::move(elements));
            return call;   // set([1, 2, 3])
        }
        consume(TokenType::COLON, "Expected ':' after dictionary key");
        auto first_value = parseExpression();
''',
        ),
        (
            "edit",
            CODEGEN,
            r'''            output << lambdaCapture() << "() { auto&& pycpp_c = ";
            generateExpression(call_expr->arguments[0].get());
            output << "; return std::" << (function_name == "list" ? "vector" : "set")
''',
            r'''            output << lambdaCapture() << "() { auto&& pycpp_c = ";
            output << listLiteralPrefix(call_expr->arguments[0].get());   // a braced list needs a real type
            generateExpression(call_expr->arguments[0].get());
            output << "; return std::" << (function_name == "list" ? "vector" : "set")
''',
        ),
        (
            "create",
            "PyCpp/tests/language/set_literals.pycpp",
            """# expect-exit: 0
# expect-stdout: {1, 2, 3} True
# expect-stdout: {'a', 'b'}
def main() -> int:
    s: set[int] = {3, 1, 2, 3}
    print(s, 2 in s)
    words: set[str] = {"b", "a"}
    print(words)
    return 0
""",
        ),
    ],
)

# ---- 7. __str__ ------------------------------------------------------------------------
group(
    "7  __str__ -> operator<<",
    has(CODEGEN, "operator<<(std::ostream"),
    [
        (
            "edit",
            CODEGEN,
            r'''        generateFunction(method.get());
    }

    dedent();
    output << getIndent() << "};\n";

    class_is_polymorphic = saved_poly;''',
            r'''        generateFunction(method.get());
    }

    // __str__ makes print(obj), str(obj) and f"{obj}" work
    if (cls->type_params.empty()) {
        for (auto& method : cls->methods) {
            if (method->name != "__str__") continue;
            output << "\n" << getIndent() << "friend std::ostream& operator<<(std::ostream& os, const " << cls->name
                   << "& self) { return os << const_cast<" << cls->name << "&>(self).__str__(); }\n";
            break;
        }
    }

    dedent();
    output << getIndent() << "};\n";

    class_is_polymorphic = saved_poly;''',
        ),
        (
            "edit",
            CODEGEN,
            '{"std::cout", "iostream"}, {"std::cin", "iostream"}, {"std::flush", "iostream"},',
            '{"std::cout", "iostream"}, {"std::cin", "iostream"}, {"std::flush", "iostream"},\n'
            '        {"std::ostream", "iostream"},',
        ),
        (
            "create",
            "PyCpp/tests/language/dunder_str.pycpp",
            """# expect-exit: 0
# expect-stdout: Point(1, 2)
# expect-stdout: p = Point(1, 2)
class Point:
    def __init__(self, x: int, y: int):
        self.x: int = x
        self.y: int = y

    def __str__(self) -> str:
        return f"Point({self.x}, {self.y})"

def main() -> int:
    p = Point(1, 2)
    print(p)
    print(f"p = {p}")
    print(str(p))
    return 0
""",
        ),
    ],
)

# ---- 8. CI -----------------------------------------------------------------------------
group(
    "8  GitHub Actions workflow",
    lambda root: (root / ".github/workflows/ci.yml").exists(),
    [
        (
            "create",
            ".github/workflows/ci.yml",
            """name: tests

on: [push, pull_request]

jobs:
  test:
    strategy:
      fail-fast: false
      matrix:
        include:
          - os: ubuntu-latest
            preset: linux-gcc-release
            compiler: gcc
          - os: macos-latest
            preset: macos-release
            compiler: clang
          - os: windows-latest
            preset: x64-release
            compiler: msvc
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4

      - name: MSVC environment
        if: runner.os == 'Windows'
        uses: ilammy/msvc-dev-cmd@v1

      - uses: actions/setup-python@v5
        with:
          python-version: '3.12'

      - name: Build the PyCPP compiler
        run: |
          cmake --preset ${{ matrix.preset }}
          cmake --build --preset ${{ matrix.preset }}

      - name: Run the regression tests
        run: python PyCpp/tests/run_tests.py --compiler ${{ matrix.compiler }}
""",
        ),
    ],
)

# ---- 9. README -------------------------------------------------------------------------
group(
    "9  README notes",
    has(README, "is a set literal"),
    [
        (
            "edit",
            README,
            "The compiler automatically detects required standard library headers.",
            "`{1, 2, 3}` is a set literal (`{}` is an empty dict). Indexing a dict with a missing key raises "
            "`KeyError`, as in Python; assigning (`d[k] = v`) inserts the key. A class that defines `__str__` "
            "can be printed, converted with `str()` and used in f-strings.\n\n"
            "The compiler automatically detects required standard library headers.",
        ),
    ],
)

# --------------------------------------------------------------------------------------
# Machinery
# --------------------------------------------------------------------------------------


def read(path):
    raw = path.read_bytes().decode("utf-8")  # a BOM stays in the text as \ufeff
    return raw.replace("\r\n", "\n"), "\r\n" in raw


def write(path, text, crlf):
    if crlf:
        text = text.replace("\n", "\r\n")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode("utf-8"))


def run_group(root, g, dry, backup_dir):
    name = g["name"]
    if g["done"](root):
        print(f"  skip   {name}  (already applied)")
        return "skipped"

    edited = {}      # rel -> (text, crlf)
    creates = {}     # rel -> content
    deletes = []
    problems = []

    for op in g["ops"]:
        kind = op[0]
        if kind == "edit":
            _, rel, old, new = op
            if rel not in edited:
                p = root / rel
                if not p.exists():
                    problems.append(f"{rel}: file not found")
                    continue
                edited[rel] = read(p)
            text, crlf = edited[rel]
            n = text.count(old)
            if n != 1:
                first = old.strip().splitlines()[0][:72] if old.strip() else repr(old)
                problems.append(f"{rel}: expected exactly 1 match, found {n}: {first}")
                continue
            edited[rel] = (text.replace(old, new, 1), crlf)
        elif kind == "create":
            _, rel, content = op
            if not (root / rel).exists():
                creates[rel] = content
        elif kind == "delete":
            _, rel = op
            if (root / rel).exists():
                deletes.append(rel)

    if problems:
        print(f"  FAIL   {name}  (nothing changed in this group)")
        for pr in problems:
            print(f"           - {pr}")
        return "failed"

    if dry:
        print(f"  would  {name}")
        for rel in edited:
            print(f"           edit   {rel}")
        for rel in creates:
            print(f"           create {rel}")
        for rel in deletes:
            print(f"           delete {rel}")
        return "dry"

    for rel in list(edited) + deletes:
        src = root / rel
        if src.exists():
            dst = backup_dir / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(src.read_bytes())
    for rel, (text, crlf) in edited.items():
        write(root / rel, text, crlf)
    for rel, content in creates.items():
        write(root / rel, content, False)
    for rel in deletes:
        (root / rel).unlink()
    print(f"  done   {name}")
    return "applied"


def find_root(arg):
    if arg:
        return Path(arg).resolve()
    here = Path(__file__).resolve().parent
    for cand in (Path.cwd(), here):
        if (cand / "PyCpp" / "src" / "main.cpp").exists():
            return cand
    return Path.cwd()


def main():
    ap = argparse.ArgumentParser(description="Apply PyCPP cleanups and small features")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--test", action="store_true", help="run the regression tests afterwards")
    ap.add_argument("--root", default=None)
    opts = ap.parse_args()

    root = find_root(opts.root)
    if not (root / "PyCpp" / "src" / "main.cpp").exists():
        print(f"error: {root} does not look like the PyCPP repo (PyCpp/src/main.cpp not found).")
        print("Run fix.py from the repo root or pass --root <dir>.")
        return 2

    backup_dir = root / ".fix_backup"
    print(f"repo: {root}\n")
    results = [run_group(root, g, opts.dry_run, backup_dir) for g in GROUPS]

    failed = results.count("failed")
    print(f"\n{results.count('applied')} applied, {results.count('skipped')} already applied, "
          f"{failed} failed" + (" (dry run)" if opts.dry_run else ""))
    if results.count("applied") and not opts.dry_run:
        print(f"originals saved in {backup_dir}")
        print("Rebuild the compiler (cmake --build --preset <preset>), then run:")
        print("    python PyCpp/tests/run_tests.py")

    if opts.test and not opts.dry_run:
        print("\nrunning tests...\n")
        code = subprocess.call([sys.executable, str(root / "PyCpp" / "tests" / "run_tests.py")])
        return code or (1 if failed else 0)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())