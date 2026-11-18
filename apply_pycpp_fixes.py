#!/usr/bin/env python3
"""
apply_pycpp_fixes.py -- applies the PyCPP fixes to your source tree in place.

Usage (from the repo root, the folder containing CMakeLists.txt):
    python apply_pycpp_fixes.py            # apply
    python apply_pycpp_fixes.py --check    # dry run, only report

What it changes
  * *p = value / *p += 1            (pointer write-dereference)
  * `unsafe:` required for deref, pointer member access, pointer indexing
  * assert cond[, "message"]        (Python-style statement -> C++ assert)
  * type mismatches in initialisation / assignment / return are errors
  * smarter type inference (operators, calls, indexing, mem.alloc[T]) and
    C++-style implicit arithmetic conversions, so valid code isn't rejected
  * <cstdint> is included automatically for i8..i64
  * syntax highlighter + README updated

All edits are all-or-nothing: if any anchor text is not found exactly once
(e.g. you changed that code), nothing is written and the failing edit is shown.
Line endings (LF or CRLF) are preserved. Re-running is safe: edits that are
already present are skipped.
"""
import sys
from pathlib import Path

ROOT = Path(".")
CHECK = "--check" in sys.argv

# Each edit: (file, mode, anchor, new_text[, end_anchor])
#   before / after : insert new_text before / after anchor
#   replace        : replace anchor with new_text
#   range          : replace from anchor up to (not including) end_anchor
#   range_incl     : replace from anchor through end_anchor
#   append         : append new_text to the end of the file
EDITS = []


def edit(path, mode, anchor, new, end=None):
    EDITS.append((path, mode, anchor, new, end))


# ---------------------------------------------------------------- parser.h
edit("PyCpp/include/pycpp_parser.h", "after",
     "    std::unique_ptr<ContinueStmt> parseContinueStmt();\n",
     "    std::unique_ptr<Stmt> parseAssertStmt();\n")

# -------------------------------------------------------------- parser.cpp
edit("PyCpp/src/pycpp_parser.cpp", "before",
     "    // Allow array subscript (arr[index])\n",
     r'''    // Pointer dereference as a target: *p = value, *p += 1
    if (auto unary = dynamic_cast<UnaryOp*>(expr)) {
        return unary->op == "*";
    }

''')

edit("PyCpp/src/pycpp_parser.cpp", "before",
     "    // Control flow statements\n",
     r'''    // assert cond[, "message"]
    if (check(TokenType::ASSERT)) {
        return parseAssertStmt();
    }

''')

edit("PyCpp/src/pycpp_parser.cpp", "append", None,
     r'''
// assert cond            ->  assert((cond))
// assert cond, "message" ->  assert((cond) && "message")
// Lowered to a call of the identifier 'assert', so no new AST node is needed.
std::unique_ptr<Stmt> PyCppParser::parseAssertStmt() {
    Token keyword = advance(); // 'assert'

    auto callee = std::make_unique<Identifier>();
    callee->name = "assert";
    callee->line = keyword.line;
    callee->column = keyword.column;

    auto call = std::make_unique<CallExpr>();
    call->callee = std::move(callee);
    call->line = keyword.line;
    call->column = keyword.column;
    call->arguments.push_back(parseExpression());
    if (match(TokenType::COMMA)) {
        call->arguments.push_back(parseExpression());
    }

    auto stmt = std::make_unique<ExprStmt>();
    stmt->expr = std::move(call);
    stmt->line = keyword.line;
    stmt->column = keyword.column;
    match(TokenType::NEWLINE);
    return stmt;
}
''')

# -------------------------------------------------------- semantic_analyzer.h
edit("PyCpp/include/semantic_analyzer.h", "after",
     "    ClassSymbol* current_class;\n",
     "    int unsafe_depth = 0; // > 0 while inside an 'unsafe:' block\n")

# -------------------------------------------------------- semantic_analyzer.cpp
SA = "PyCpp/src/semantic_analyzer.cpp"

edit(SA, "after",
     '    addSymbol("mem.free", "function");\n',
     '    addSymbol("assert", "function");\n')

edit(SA, "replace",
     '''        "int", "float", "double", "bool", "str", "string", "void", "auto",
        "std::string"
    };''',
     '''        "int", "float", "double", "bool", "str", "string", "void", "auto",
        "std::string", "char",
        "int8_t", "int16_t", "int32_t", "int64_t"
    };''')

edit(SA, "before",
     "    // Allow array subscript (arr[index])\n",
     r'''    // Allow pointer dereference (*p = value)
    if (auto unary = dynamic_cast<UnaryOp*>(expr)) {
        return unary->op == "*";
    }

''')

edit(SA, "range",
     "// Type Compatibility - FIXED\n",
     r'''// Type Compatibility
static bool isNumericType(const std::string& t) {
    static const std::unordered_set<std::string> numeric = {
        "int", "float", "double", "bool", "char",
        "int8_t", "int16_t", "int32_t", "int64_t"
    };
    return numeric.count(t) > 0;
}

bool SemanticAnalyzer::typesAreCompatible(const std::string& type1, const std::string& type2) {
    if (type1 == type2) return true;
    if (type1 == "auto" || type2 == "auto") return true;
    if (type1 == "unknown" || type2 == "unknown") return true;
    if (generic_params.count(type1) || generic_params.count(type2)) return true;

    auto isStr = [](const std::string& t) {
        return t == "str" || t == "string" || t == "std::string";
    };
    if (isStr(type1) && isStr(type2)) return true;

    // C++ semantics: arithmetic types convert implicitly
    if (isNumericType(type1) && isNumericType(type2)) return true;

    // list[T]: compare element types ("std::vector<auto>" matches anything)
    static const std::string vec = "std::vector<";
    if (type1.rfind(vec, 0) == 0 && type2.rfind(vec, 0) == 0 &&
        type1.back() == '>' && type2.back() == '>') {
        return typesAreCompatible(type1.substr(vec.size(), type1.size() - vec.size() - 1),
                                  type2.substr(vec.size(), type2.size() - vec.size() - 1));
    }

    // void* converts to any pointer type
    if ((type1 == "void*" && !type2.empty() && type2.back() == '*') ||
        (type2 == "void*" && !type1.empty() && type1.back() == '*')) return true;

    return false;
}

''',
     end="std::string SemanticAnalyzer::typeToString(Type* type) {")

edit(SA, "range_incl",
     '''    else if (auto bin_op = dynamic_cast<BinaryOp*>(expr)) {
        // For binary operations, return the type of the left operand
''',
     r'''    else if (auto unary = dynamic_cast<UnaryOp*>(expr)) {
        std::string inner = getExpressionType(unary->operand.get());
        if (unary->op == "*") {
            if (!inner.empty() && inner.back() == '*') return inner.substr(0, inner.size() - 1);
            return "auto";
        }
        if (unary->op == "&") return (inner == "auto" || inner == "unknown") ? "auto" : inner + "*";
        if (unary->op == "not") return "bool";
        return inner;
    }
    else if (auto bin_op = dynamic_cast<BinaryOp*>(expr)) {
        static const std::unordered_set<std::string> bool_ops = {
            "<", ">", "<=", ">=", "==", "!=", "and", "or", "is", "is not", "in", "not in"
        };
        if (bool_ops.count(bin_op->op)) return "bool";
        // otherwise the type of the left operand
        return getExpressionType(bin_op->left.get());
    }
    else if (auto call = dynamic_cast<CallExpr*>(expr)) {
        if (call->is_subscript) { // a[i]
            std::string container = getExpressionType(call->callee.get());
            static const std::string vec = "std::vector<";
            if (container.rfind(vec, 0) == 0 && container.back() == '>')
                return container.substr(vec.size(), container.size() - vec.size() - 1);
            if (!container.empty() && container.back() == '*')
                return container.substr(0, container.size() - 1);
            return "auto";
        }
        if (auto mem = dynamic_cast<MemberAccess*>(call->callee.get())) {
            auto base = dynamic_cast<Identifier*>(mem->object.get());
            if (base && base->name == "mem" && mem->member == "alloc" && !call->type_args.empty())
                return typeToString(call->type_args[0].get()) + "*"; // mem.alloc[T]() -> T*
            return "auto";
        }
        if (auto ident = dynamic_cast<Identifier*>(call->callee.get())) {
            const std::string& n = ident->name;
            if (n == "print") return "void";
            if (n == "len") return "int";
            if (n == "str") return "std::string";
            if (n == "mem.free") return "void";
            auto fn = functions.find(n);
            if (fn != functions.end()) return fn->second.return_type; // user function
            if (classes.count(n)) return n;                           // constructor
        }
        return "auto";
    }
    return "auto";
}
''',
     end='''        return "auto";
    }
    return "auto";
}
''')

# analyzeExpression: pointer indexing needs unsafe
edit(SA, "after",
     '''    else if (auto call = dynamic_cast<CallExpr*>(expr)) {
        pycpp::debugLog() << "Found function call" << std::endl;
''',
     r'''        if (call->is_subscript && unsafe_depth == 0) {
            if (auto ptr_ident = dynamic_cast<Identifier*>(call->callee.get())) {
                Symbol* sym = findSymbol(ptr_ident->name);
                if (sym && !sym->type.empty() && sym->type.back() == '*')
                    error("Indexing pointer '" + ptr_ident->name + "' requires an 'unsafe:' block",
                        call->line, call->column);
            }
        }
''')

# analyzeExpression: pointer member access needs unsafe
edit(SA, "after",
     '''        // Handle regular member access like self.health
        analyzeExpression(member_access->object.get());
''',
     r'''        if (auto obj = dynamic_cast<Identifier*>(member_access->object.get())) {
            Symbol* sym = findSymbol(obj->name);
            if (sym && !sym->type.empty() && sym->type.back() == '*' && unsafe_depth == 0)
                error("Accessing '" + member_access->member + "' through pointer '" + obj->name +
                    "' requires an 'unsafe:' block", member_access->line, member_access->column);
        }
''')

# analyzeExpression: dereference needs unsafe
edit(SA, "replace",
     '''        pycpp::debugLog() << "Found unary operation: " << unary_op->op << std::endl;
        analyzeExpression(unary_op->operand.get());
''',
     '''        pycpp::debugLog() << "Found unary operation: " << unary_op->op << std::endl;
        if (unary_op->op == "*" && unsafe_depth == 0)
            error("Pointer dereference requires an 'unsafe:' block", unary_op->line, unary_op->column);
        analyzeExpression(unary_op->operand.get());
''')

# plain assignment mismatch -> error, other operators stay warnings
edit(SA, "replace",
     '''            if (!typesAreCompatible(left_type, right_type)) {
                warning("Type mismatch in binary operation: " + left_type + " vs " + right_type,
                    bin_op->line, bin_op->column);
            }''',
     '''            if (!typesAreCompatible(left_type, right_type)) {
                if (bin_op->op == "=")
                    error("Cannot assign " + right_type + " to " + left_type, bin_op->line, bin_op->column);
                else
                    warning("Type mismatch in binary operation: " + left_type + " vs " + right_type,
                        bin_op->line, bin_op->column);
            }''')

edit(SA, "replace",
     '''            warning("Type mismatch in variable initialization: " + type_name + " vs " + expr_type,
                var->line, var->column);''',
     '''            error("Cannot initialize '" + var->var_name + "' of type " + type_name +
                " with a value of type " + expr_type, var->line, var->column);''')

edit(SA, "replace",
     '''                warning("Return type mismatch: " + current_function->return_type + " vs " + return_type,
                    return_stmt->line, return_stmt->column);''',
     '''                error("Return type mismatch: function returns " + current_function->return_type +
                    " but got " + return_type, return_stmt->line, return_stmt->column);''')

# track unsafe depth
edit(SA, "replace",
     '''    else if (auto unsafe_block = dynamic_cast<UnsafeBlock*>(stmt)) {
        enterScope();
        for (auto& stmt : unsafe_block->body) {
            analyzeStatement(stmt.get());
        }
        exitScope();
    }''',
     '''    else if (auto unsafe_block = dynamic_cast<UnsafeBlock*>(stmt)) {
        unsafe_depth++;
        enterScope();
        for (auto& inner : unsafe_block->body) {
            analyzeStatement(inner.get());
        }
        exitScope();
        unsafe_depth--;
    }''')

edit(SA, "replace",
     '''void SemanticAnalyzer::visit(UnsafeBlock& node) {
    enterScope();
    for (auto& stmt : node.body) { stmt->accept(*this); }
    exitScope();
}''',
     '''void SemanticAnalyzer::visit(UnsafeBlock& node) {
    unsafe_depth++;
    enterScope();
    for (auto& stmt : node.body) { stmt->accept(*this); }
    exitScope();
    unsafe_depth--;
}''')

# ----------------------------------------------------------- code_generator.cpp
CG = "PyCpp/src/code_generator.cpp"

edit(CG, "replace",
     '''            bool needs_parens = !dynamic_cast<Identifier*>(bin_op->left.get()) &&
                !dynamic_cast<MemberAccess*>(bin_op->left.get()) &&
                !dynamic_cast<CallExpr*>(bin_op->left.get());''',
     '''            bool needs_parens = !dynamic_cast<Identifier*>(bin_op->left.get()) &&
                !dynamic_cast<MemberAccess*>(bin_op->left.get()) &&
                !dynamic_cast<CallExpr*>(bin_op->left.get()) &&
                !(dynamic_cast<UnaryOp*>(bin_op->left.get()) &&
                  static_cast<UnaryOp*>(bin_op->left.get())->op == "*");''')

edit(CG, "before",
     '''        if (function_name == "print") {
            require("iostream");''',
     r'''        if (function_name == "assert") {
            require("assert"); // -> <cassert>
            if (call_expr->arguments.empty()) { output << "assert(true)"; return; }
            // Double parentheses keep commas inside templates from splitting the macro
            output << "assert((";
            generateExpression(call_expr->arguments[0].get());
            output << ")";
            if (call_expr->arguments.size() > 1) {
                output << " && ";
                generateExpression(call_expr->arguments[1].get());
            }
            output << ")";
            return;
        }
        else ''')

edit(CG, "replace",
     '''        {"std::shared_ptr", "memory"}, {"std::make_shared", "memory"},
    };''',
     '''        {"std::shared_ptr", "memory"}, {"std::make_shared", "memory"},
        {"int8_t", "cstdint"}, {"int16_t", "cstdint"}, {"int32_t", "cstdint"}, {"int64_t", "cstdint"},
    };''')

# ------------------------------------------------------------ include_manager.h
edit("PyCpp/include/include_manager.h", "after",
     '        {"type_traits", {"<type_traits>"}},\n',
     '        {"cstdint", {"<cstdint>"}},\n')

# ------------------------------------------------------------ syntax highlighter
edit("Syntax_highlighter/syntaxes/pycpp.tmLanguage.json", "replace",
     "(if|elif|else|for|while|break|continue|return|use|unsafe)",
     "(if|elif|else|for|while|break|continue|return|assert|use|unsafe)")

# ------------------------------------------------------------ README
RD = "README.md"

edit(RD, "after",
     "- `break`, `continue`, `return` — flow control.\n",
     "- `assert` — runtime check with an optional message: `assert x > 0, \"x must be positive\"`. "
     "Compiles to C++ `assert` (disabled by `NDEBUG`).\n")

edit(RD, "replace",
     "Type safety is enforced at compile time.\n",
     "Type safety is enforced at compile time. Initialising a variable, assigning, or returning a value "
     "of an incompatible type is a **compile error** and stops the build. Arithmetic types convert "
     "implicitly, exactly as in C++. Suspicious-but-legal code (for example a mismatched operator) "
     "produces a warning and does not stop the build.\n")

edit(RD, "before",
     "Features:\n\n- `mem.alloc()` — allocate memory.",
     '''Reading or writing through a pointer, accessing members through a pointer, and indexing a pointer are only allowed inside `unsafe:`. Taking an address (`&x`) and `mem.alloc` / `mem.free` are safe.

```
def main() -> int:
    value: int = 42
    pointer: *int = &value
    unsafe:
        *pointer = 100      # write through a pointer
        *pointer += 1
    return 0
```

Dereferencing outside `unsafe:` is a compile error.

''')

edit(RD, "replace",
     "A syntax error stops the build. The parser never skips a statement it cannot understand.",
     "Syntax errors and type errors stop the build. The parser never skips a statement it cannot understand.")


# ======================================================================= engine
def apply_edit(text, mode, anchor, new, end):
    """Return (new_text, status) where status is 'ok' or 'skip' or raises ValueError."""
    if mode == "append":
        if new.strip() in text:
            return text, "skip"
        if not text.endswith("\n"):
            text += "\n"
        return text + new, "ok"

    if new.strip() and new.strip() in text and mode in ("before", "after", "replace"):
        return text, "skip"
    if mode in ("range", "range_incl") and new.strip() in text:
        return text, "skip"

    n = text.count(anchor)
    if n != 1:
        raise ValueError(f"anchor found {n} times (need exactly 1):\n---\n{anchor}---")
    i = text.index(anchor)

    if mode == "before":
        return text[:i] + new + text[i:], "ok"
    if mode == "after":
        j = i + len(anchor)
        return text[:j] + new + text[j:], "ok"
    if mode == "replace":
        return text[:i] + new + text[i + len(anchor):], "ok"
    if mode in ("range", "range_incl"):
        k = text.find(end, i + len(anchor))
        if k < 0:
            raise ValueError(f"end anchor not found after start anchor:\n---\n{end}---")
        k_end = k + len(end) if mode == "range_incl" else k
        return text[:i] + new + text[k_end:], "ok"
    raise ValueError("unknown mode " + mode)


def main():
    by_file = {}
    for e in EDITS:
        by_file.setdefault(e[0], []).append(e)

    results = {}
    failed = False
    for path, edits in by_file.items():
        p = ROOT / path
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
        p.write_bytes(text.encode("utf-8"))
    print(f"\nPatched {len(results)} files.")


if __name__ == "__main__":
    main()
