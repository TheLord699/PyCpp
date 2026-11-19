#!/usr/bin/env python3
"""
fix4.py -- exception handling: try / except / else / finally and raise.
Run AFTER fix.py and fix2.py (fix3.py is optional), from the repo root:
    python fix4.py            # apply
    python fix4.py --check    # dry run

Syntax
    try:
        ...
    except ValueError as e:        # `as e` is optional
        ...
    except (TypeError, KeyError):  # several types
        ...
    except:                        # everything
        ...
    else:                          # runs when the try body raised nothing
        ...
    finally:                       # always runs, even on return/break/continue
        ...

    raise ValueError("message")    # raise an exception
    raise MyError                  # same as MyError()
    raise                          # re-raise inside an except block

Built-in exceptions: Exception, ArithmeticError, ZeroDivisionError, OverflowError,
LookupError, IndexError, KeyError, ValueError, TypeError, RuntimeError.
Define your own with `class MyError(Exception):`; a leading `str` parameter of
its __init__ becomes the message. print(e) / str(e) give the message.

What changes elsewhere
  * Runtime errors (int("abc"), 1 / 0, ...) now THROW instead of exiting, so they
    can be caught. Uncaught, they still print "ValueError: message" and exit 1.
  * Generated C++ uses real try/catch; `finally` is a pycpp::Defer object.

Known limit: a variable declared inside a try body is not visible after it (C++
scoping). Declare it before the try.
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


AST_H = "PyCpp/include/ast.h"
AST_CPP = "PyCpp/src/ast.cpp"
PAR_H = "PyCpp/include/pycpp_parser.h"
PAR = "PyCpp/src/pycpp_parser.cpp"
SA_H = "PyCpp/include/semantic_analyzer.h"
SA = "PyCpp/src/semantic_analyzer.cpp"
CGH = "PyCpp/include/code_generator.h"
CG = "PyCpp/src/code_generator.cpp"
RT = "PyCpp/include/pycpp_runtime_text.h"
README = "README.md"

# ================================================================== AST
edit(AST_H, "before", r'''class Program : public ASTNode {''',
     r'''// except [Type | (Type, Type)] [as name]:
struct ExceptClause {
    std::vector<std::string> type_names;   // empty: `except:` catches everything
    std::string var_name;                  // `as e`; empty when absent
    std::vector<std::unique_ptr<Stmt>> body;
    int line = 0, column = 0;
};

class TryStmt : public Stmt {
public:
    std::vector<std::unique_ptr<Stmt>> body;
    std::vector<ExceptClause> handlers;
    std::vector<std::unique_ptr<Stmt>> else_body;
    std::vector<std::unique_ptr<Stmt>> finally_body;
    void accept(ASTVisitor& visitor) override;
};

class RaiseStmt : public Stmt {
public:
    std::unique_ptr<Expr> value;           // null: bare `raise` (re-raise)
    void accept(ASTVisitor& visitor) override;
};

''')
edit(AST_H, "after", r'''virtual void visit(DeferStmt&) {}''',
     r'''
    virtual void visit(TryStmt&) {}
    virtual void visit(RaiseStmt&) {}''')

edit(AST_CPP, "before", r'''void DeferStmt::accept(ASTVisitor& visitor) {''',
     r'''void TryStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void RaiseStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

''')

# ================================================================== parser
edit(PAR_H, "after", r'''std::unique_ptr<Stmt> parseDeferStmt();''',
     r'''
    std::unique_ptr<Stmt> parseTryStmt();
    std::unique_ptr<Stmt> parseRaiseStmt();''')

edit(PAR, "after", r'''if (check(TokenType::DEFER)) { advance(); return parseDeferStmt(); }''',
     r'''
    if (check(TokenType::TRY)) { advance(); return parseTryStmt(); }
    if (check(TokenType::RAISE)) { advance(); return parseRaiseStmt(); }''')

edit(PAR, "before", r'''// defer stmt      |      defer:  <indented block>''',
     r'''// try:        <block>
// except T as e:  <block>      (T may be (A, B); the whole clause may be just `except:`)
// else:       <block>
// finally:    <block>
std::unique_ptr<Stmt> PyCppParser::parseTryStmt() {
    auto t = std::make_unique<TryStmt>();
    t->line = tokens[position - 1].line;
    t->column = tokens[position - 1].column;

    // ':' then either an indented block or a single statement on the same line
    auto suite = [&](const std::string& what) {
        consume(TokenType::COLON, "Expected ':' after " + what);
        std::vector<std::unique_ptr<Stmt>> body;
        if (check(TokenType::NEWLINE)) {
            advance();
            body = parseBlock();
        }
        else if (auto s = parseStatement()) {
            body.push_back(std::move(s));
        }
        return body;
    };

    t->body = suite("'try'");
    handleIndentation();

    while (check(TokenType::EXCEPT)) {
        ExceptClause c;
        Token keyword = advance();
        c.line = keyword.line;
        c.column = keyword.column;
        if (!check(TokenType::COLON)) {
            if (match(TokenType::LPAREN)) {
                do {
                    c.type_names.push_back(consume(TokenType::IDENTIFIER, "Expected an exception type").value);
                } while (match(TokenType::COMMA));
                consume(TokenType::RPAREN, "Expected ')' after exception types");
            }
            else {
                c.type_names.push_back(consume(TokenType::IDENTIFIER, "Expected an exception type").value);
            }
            if (match(TokenType::AS)) {
                c.var_name = consume(TokenType::IDENTIFIER, "Expected a name after 'as'").value;
            }
        }
        c.body = suite("'except'");
        handleIndentation();
        t->handlers.push_back(std::move(c));
    }

    if (!t->handlers.empty() && check(TokenType::ELSE)) {
        advance();
        t->else_body = suite("'else'");
        handleIndentation();
    }

    if (match(TokenType::FINALLY)) {
        t->finally_body = suite("'finally'");
    }

    if (t->handlers.empty() && t->finally_body.empty()) {
        throw std::runtime_error("'try' needs at least one 'except' or a 'finally'");
    }
    return t;
}

// raise Type("message")  |  raise Type  |  raise
std::unique_ptr<Stmt> PyCppParser::parseRaiseStmt() {
    auto r = std::make_unique<RaiseStmt>();
    r->line = tokens[position - 1].line;
    r->column = tokens[position - 1].column;
    if (!check(TokenType::NEWLINE) && !check(TokenType::DEDENT) && !atEnd()) {
        r->value = parseExpression();
    }
    match(TokenType::NEWLINE);
    return r;
}

''')

# ================================================================== semantic analyzer
edit(SA_H, "after", r'''void analyzeMatchStmt(MatchStmt* m);''',
     r'''
    void analyzeTryStmt(TryStmt* t);
    void analyzeRaiseStmt(RaiseStmt* r);
    int handler_depth = 0;   // > 0 while analysing the body of an except clause''')

edit(SA, "after", r'''addSymbol("nullptr", "void*");''',
     r'''
    for (const char* name : { "Exception", "ArithmeticError", "ZeroDivisionError", "OverflowError",
                              "LookupError", "IndexError", "KeyError", "ValueError", "TypeError",
                              "RuntimeError" }) {
        addSymbol(name, "class");
    }''')

edit(SA, "before", r'''// Type Checking - FIXED''',
     r'''// Exception types provided by the runtime
static bool isBuiltinException(const std::string& name) {
    static const std::unordered_set<std::string> names = {
        "Exception", "ArithmeticError", "ZeroDivisionError", "OverflowError",
        "LookupError", "IndexError", "KeyError", "ValueError", "TypeError", "RuntimeError"
    };
    return names.count(name) > 0;
}

''')

edit(SA, "before", r'''// Generic type parameters (T in def f[T] / class C[T])''',
     r'''if (isBuiltinException(type)) {
        return true;
    }

    ''')

edit(SA, "after", r'''analyzeDeferStmt(defer_stmt);''',
     r'''
    }
    else if (auto try_stmt = dynamic_cast<TryStmt*>(stmt)) {
        analyzeTryStmt(try_stmt);
    }
    else if (auto raise_stmt = dynamic_cast<RaiseStmt*>(stmt)) {
        analyzeRaiseStmt(raise_stmt);''')

edit(SA, "before", r'''// Visitor pattern implementations (delegate to analysis methods)''',
     r'''void SemanticAnalyzer::analyzeTryStmt(TryStmt* t) {
    if (!current_function)
        error("'try' can only be used inside a function", t->line, t->column,
              "put the try statement inside a def");

    enterScope();
    for (auto& s : t->body) analyzeStatement(s.get());
    exitScope();

    for (auto& h : t->handlers) {
        for (const auto& name : h.type_names) {
            if (!isBuiltinException(name) && !classes.count(name) && !findSymbol(name))
                warning("unknown exception type '" + name + "'", h.line, h.column,
                        "use a built-in one (Exception, ValueError, ZeroDivisionError, ...) "
                        "or define it with: class " + name + "(Exception)");
        }
        enterScope();
        if (!h.var_name.empty())
            addSymbol(h.var_name, h.type_names.size() == 1 ? h.type_names[0] : std::string("Exception"), true);
        handler_depth++;
        for (auto& s : h.body) analyzeStatement(s.get());
        handler_depth--;
        exitScope();
    }

    if (!t->else_body.empty()) {
        enterScope();
        for (auto& s : t->else_body) analyzeStatement(s.get());
        exitScope();
    }

    for (auto& s : t->finally_body) {
        if (dynamic_cast<ReturnStmt*>(s.get()) || dynamic_cast<BreakStmt*>(s.get()) ||
            dynamic_cast<ContinueStmt*>(s.get()))
            error("'return', 'break' and 'continue' are not allowed directly inside a finally block",
                  s->line, s->column);
    }
    enterScope();
    for (auto& s : t->finally_body) analyzeStatement(s.get());
    exitScope();
}

void SemanticAnalyzer::analyzeRaiseStmt(RaiseStmt* r) {
    if (!current_function)
        error("'raise' can only be used inside a function", r->line, r->column);
    // A function that ends in `raise` does not need a return value
    current_function->has_return = true;
    if (!r->value) {
        if (handler_depth == 0)
            error("'raise' with no exception to re-raise", r->line, r->column,
                  "use `raise ValueError(\"message\")`, or put a bare `raise` inside an except block");
        return;
    }
    analyzeExpression(r->value.get());
}

''')

# ================================================================== code generator
edit(CGH, "after",
     r'''const char* lambdaCapture() const { return current_function ? "[&]" : "[]"; }''',
     r'''

    std::vector<std::string> handler_vars;   // names bound by the `except ... as e` clauses being generated
    int try_counter = 0;''')
edit(CGH, "after", r'''void visit(MatchStmt& node) override;''',
     r'''
    void visit(TryStmt& node) override;
    void visit(RaiseStmt& node) override;''')

edit(CG, "before",
     r'''CodeGenerator::CodeGenerator() : indent_level(0), current_function(nullptr), current_class(nullptr) {''',
     r'''// Exceptions the runtime provides (user classes may derive from them)
static bool isBuiltinExceptionName(const std::string& name) {
    static const std::set<std::string> names = {
        "Exception", "ArithmeticError", "ZeroDivisionError", "OverflowError",
        "LookupError", "IndexError", "KeyError", "ValueError", "TypeError", "RuntimeError"
    };
    return names.count(name) > 0;
}

''')

# class MyError(Exception) needs the runtime header
edit(CG, "before", r'''output << getIndent() << "class " << cls->name;''',
     r'''if (isBuiltinExceptionName(cls->base_class)) useRuntime();
    ''')

# class MyError(Exception): __init__(self, message: str) -> base gets the message
edit(CG, "after", r'''collectConstructorInitializers(func, inits, consumed);''',
     r'''
        if (current_class && isBuiltinExceptionName(current_class->base_class)) {
            for (const auto& p : func->parameters) {
                if (p.name == "self" || !p.type) continue;
                auto basic = dynamic_cast<BasicType*>(p.type.get());
                if (basic && (basic->name == "std::string" || basic->name == "str"))
                    inits.insert(inits.begin(), { current_class->base_class, p.name });
                break;   // only the first real parameter can be the message
            }
        }''')

edit(CG, "before", r'''// ===== match / case =====''',
     r'''// ===== try / except / else / finally / raise =====
// try/except become C++ try/catch. `finally` is a pycpp::Defer object declared first, so
// it is destroyed last: it runs after the handlers, on return/break/continue, and while
// an exception propagates. `else` runs when the try body finished without throwing.
void CodeGenerator::visit(TryStmt& node) {
    useRuntime();
    const int id = ++try_counter;
    const std::string ok = "pycpp_try_ok_" + std::to_string(id);
    const bool has_else = !node.else_body.empty();

    auto emitBody = [&](const std::vector<std::unique_ptr<Stmt>>& body, const std::string& tail = "") {
        indent(); pushScope();
        for (auto& s : body) generateStatement(s.get());
        if (!tail.empty()) output << getIndent() << tail << "\n";
        popScope(); dedent();
    };

    output << getIndent() << "{\n";
    indent();

    if (!node.finally_body.empty()) {
        output << getIndent() << "pycpp::Defer pycpp_finally_" << id << "([&]() {\n";
        emitBody(node.finally_body);
        output << getIndent() << "});\n";
    }
    if (has_else) output << getIndent() << "bool " << ok << " = false;\n";

    // `try { }` with no catch is not valid C++ (a try/finally has no handlers)
    output << getIndent() << (node.handlers.empty() ? "{\n" : "try {\n");
    emitBody(node.body, has_else ? ok + " = true;" : "");
    output << getIndent() << "}\n";

    for (auto& h : node.handlers) {
        std::vector<std::string> types = h.type_names;
        if (types.empty()) types.push_back("");          // `except:` -> catch (...)
        for (const auto& type : types) {                 // except (A, B): one catch per type
            output << getIndent() << "catch (";
            if (type.empty()) {
                output << "...";
            }
            else {
                output << "const " << type << "&";
                if (!h.var_name.empty()) output << " " << h.var_name;
            }
            output << ") {\n";
            indent(); pushScope();
            if (!h.var_name.empty()) declareVar(h.var_name, nullptr);
            handler_vars.push_back(h.var_name);
            for (auto& s : h.body) generateStatement(s.get());
            handler_vars.pop_back();
            popScope(); dedent();
            output << getIndent() << "}\n";
        }
    }

    if (has_else) {
        output << getIndent() << "if (" << ok << ") {\n";
        emitBody(node.else_body);
        output << getIndent() << "}\n";
    }

    dedent();
    output << getIndent() << "}\n";
}

void CodeGenerator::visit(RaiseStmt& node) {
    useRuntime();
    output << getIndent();
    if (!node.value) {
        output << "throw;\n";                             // re-raise the exception being handled
        return;
    }
    if (auto id = dynamic_cast<Identifier*>(node.value.get())) {
        bool is_handler_var = false;
        for (const auto& v : handler_vars) if (!v.empty() && v == id->name) is_handler_var = true;
        if (is_handler_var) output << "throw;\n";         // raise e
        else output << "throw " << id->name << "();\n";   // raise MyError
        return;
    }
    output << "throw ";
    generateExpression(node.value.get());
    output << ";\n";
}

''')

# ================================================================== runtime header
edit(RT, "replace", r'''#include <cmath>''', r'''#include <cmath>
#include <exception>''')

# exceptions replace the old print-and-exit fail()
edit(RT, "replace", r'''[[noreturn]] inline void fail(const char* kind, const std::string& message) {
    std::fprintf(stderr, "%s: %s\n", kind, message.c_str());
    std::exit(1);
}''',
     r'''// PyCPP exceptions. They deliberately do not derive from std::exception, so the runtime's
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
}();''')

# print(e) / str(e)
edit(RT, "after", r'''else if constexpr (std::is_same_v<T, bool>) return v ? "True" : "False";''',
     r'''
    else if constexpr (std::is_base_of_v<Exception, T>) return v.what();''')

# the names are used unqualified in generated code (except ValueError as e, raise MyError)
edit(RT, "after", r'''} // namespace pycpp''',
     r'''

using pycpp::Exception;
using pycpp::ArithmeticError;
using pycpp::ZeroDivisionError;
using pycpp::OverflowError;
using pycpp::LookupError;
using pycpp::IndexError;
using pycpp::KeyError;
using pycpp::ValueError;
using pycpp::TypeError;
using pycpp::RuntimeError;''')

# ================================================================== README
edit(README, "after", r'''- `pycpp` exits with the exit code of the program it ran.''',
     r'''
- Exceptions: `try` / `except` / `else` / `finally` and `raise`.
  ```
  def main() -> int:
      try:
          n: int = int("abc")
      except (ValueError, TypeError) as e:
          print(e)
      finally:
          print("done")
      return 0
  ```
  Built-in exceptions: `Exception`, `ArithmeticError`, `ZeroDivisionError`, `OverflowError`, `LookupError`, `IndexError`, `KeyError`, `ValueError`, `TypeError`, `RuntimeError`. Define your own with `class MyError(Exception):`; a leading `str` parameter of its `__init__` becomes its message. Errors such as `int("abc")` and `1 / 0` raise catchable exceptions; uncaught, they print `ValueError: message` and exit with code 1. A variable declared inside a `try` body is not visible after it, so declare it before the `try`.''')
edit(README, "replace", r'''- Exception handling.
- `try` / `except`.''', r'''- `super()` for custom exception classes.''')
edit(README, "replace", r'''- Slicing.
- Exception handling.''', r'''- Slicing.
- Exception chaining (`raise ... from ...`).''')

# ================================================================== example
edit("PyCpp/examples/simple/exceptions.pycpp", "create", None,
     '''class AppError(Exception):
    def __init__(self, message: str):
        self.code: int = 7

def risky(n: int) -> int:
    if n < 0:
        raise ValueError("negative number")
    if n == 0:
        raise AppError("zero is not allowed")
    return 100 // n

def main() -> int:
    values: list[int] = [5, -1, 0]
    for n in values:
        try:
            print(risky(n))
        except ValueError as e:
            print("ValueError:", e)
        except AppError as e:
            print("AppError:", e)
        finally:
            print("done")

    try:
        zero: int = 0
        print(10 / zero)
    except ZeroDivisionError as e:
        print(e)

    try:
        number: int = int("abc")
        print(number)
    except (ValueError, TypeError):
        print("not a number")

    try:
        parsed: int = int("42")
        print(parsed)
    except ValueError:
        print("bad")
    else:
        print("parsed ok")
    finally:
        print("cleanup")

    try:
        try:
            raise RuntimeError("inner")
        except RuntimeError as e:
            print("caught inner")
            raise
    except Exception as e:
        print("caught outer:", e)

    return 0
''')


# ======================================================================= engine
def apply_edit(text, mode, anchor, new, end):
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
        print("fix.py has not been applied yet. Run fix.py and fix2.py first, then fix4.py.")
        sys.exit(1)
    if rt.exists() and "container_str" not in rt.read_text(encoding="utf-8"):
        print("fix2.py has not been applied yet. Run it first, then fix4.py.")
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