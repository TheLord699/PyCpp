#!/usr/bin/env python3
"""
fix.py - patches PyCPP's import handling.

  1. `import a.b as x`          (alias support)
  2. `import a, b`              (comma-separated imports)
  3. `import g` + `import g.s`  (no duplicate-symbol error)
  4. `import g.s`               (#include "s.h", not "g.h")

Run from the repository root (the folder that contains PyCpp/):

    python fix.py

All edits are applied in memory first. If any anchor text is not found exactly
once, nothing is written. Running it a second time is a no-op.
"""
import sys
from pathlib import Path

ROOT = Path(".")
files = {}  # path -> [text, crlf, bom]
failures = []


def load(rel):
    if rel not in files:
        raw = (ROOT / rel).read_bytes()
        bom = raw.startswith(b"\xef\xbb\xbf")
        if bom:
            raw = raw[3:]
        crlf = b"\r\n" in raw
        text = raw.decode("utf-8").replace("\r\n", "\n")
        files[rel] = [text, crlf, bom]
    return files[rel]


def patch(rel, old, new, marker=None):
    """Replace `old` (must occur exactly once) with `new`.
    `marker` is text that only exists once the patch is applied."""
    entry = load(rel)
    text = entry[0]
    if (marker or new) in text and old not in text:
        print(f"  skip   {rel}: already applied")
        return
    n = text.count(old)
    if n != 1:
        failures.append(f"{rel}: expected 1 match, found {n} for:\n{old[:120]!r}")
        return
    entry[0] = text.replace(old, new)
    print(f"  patch  {rel}")


def patch_between(rel, start, end, new, marker):
    entry = load(rel)
    text = entry[0]
    if marker in text:
        print(f"  skip   {rel}: already applied")
        return
    if text.count(start) != 1 or text.count(end) != 1:
        failures.append(f"{rel}: region markers not found exactly once: {start[:60]!r} .. {end[:60]!r}")
        return
    i = text.index(start)
    j = text.index(end, i)
    entry[0] = text[:i] + new + text[j:]
    print(f"  patch  {rel}")


# ---------------------------------------------------------------- ast.h
patch(
    "PyCpp/include/ast.h",
    r'''class ImportStmt : public Stmt {
public:
    std::string module;
    std::vector<std::string> imports;
    bool is_from = false; // from module import a, b
    void accept(ASTVisitor& visitor) override;
};''',
    r'''class ImportStmt : public Stmt {
public:
    std::string module;                  // full dotted path: "graphics.shapes"
    std::string alias;                   // `import x as alias` (empty if absent)
    std::vector<std::string> imports;
    bool is_from = false;                // from module import a, b

    // Generated files are flattened into temp/ and named after the file stem,
    // so graphics.shapes -> "shapes" (shapes.h / shapes.cpp).
    std::string leaf() const {
        const auto dot = module.rfind('.');
        return dot == std::string::npos ? module : module.substr(dot + 1);
    }
    void accept(ASTVisitor& visitor) override;
};''',
    marker="std::string leaf() const",
)

# ------------------------------------------------------- pycpp_parser.h
patch(
    "PyCpp/include/pycpp_parser.h",
    "    std::set<std::string> generic_names; // functions/classes declared with [T] parameters\n",
    "    std::set<std::string> generic_names; // functions/classes declared with [T] parameters\n"
    "    std::vector<std::unique_ptr<Stmt>> pending_statements; // extra imports from `import a, b`\n",
    marker="pending_statements;",
)
patch(
    "PyCpp/include/pycpp_parser.h",
    "    std::unique_ptr<ImportStmt> parseFromImportStmt();\n",
    "    std::unique_ptr<ImportStmt> parseFromImportStmt();\n"
    "    std::string parseModulePath();\n",
    marker="parseModulePath();",
)

# ----------------------------------------------------- pycpp_parser.cpp
patch_between(
    "PyCpp/src/pycpp_parser.cpp",
    "// from module import name1, name2\n",
    "std::unique_ptr<UseStmt> PyCppParser::parseUseStmt() {",
    r'''// graphics.shapes -> "graphics.shapes" (any word is allowed, even keyword-lexed ones like `random`)
std::string PyCppParser::parseModulePath() {
    if (atEnd() || !isWordToken(peek()))
        throw std::runtime_error("Expected module name at line " +
            std::to_string(peek().line) + ":" + std::to_string(peek().column));
    std::string path = advance().value;
    while (check(TokenType::DOT)) {
        advance();
        if (atEnd() || !isWordToken(peek()))
            throw std::runtime_error("Expected a name after '.' in module path");
        path += "." + advance().value;
    }
    return path;
}

// from a.b import name1, name2
std::unique_ptr<ImportStmt> PyCppParser::parseFromImportStmt() {
    auto import_stmt = std::make_unique<ImportStmt>();
    import_stmt->line = tokens[position - 1].line;
    import_stmt->column = tokens[position - 1].column;
    import_stmt->is_from = true;

    import_stmt->module = parseModulePath();
    consume(TokenType::IMPORT, "Expected 'import' after module name");

    do {
        import_stmt->imports.push_back(consume(TokenType::IDENTIFIER, "Expected name after 'import'").value);
    } while (match(TokenType::COMMA));

    match(TokenType::NEWLINE);
    return import_stmt;
}

// import a.b [as x] [, c [as y] ...]
// The first module is returned; the rest are queued in pending_statements and
// appended to the program by parseProgram().
std::unique_ptr<ImportStmt> PyCppParser::parseImportStmt() {
    const int line = tokens[position - 1].line;
    const int column = tokens[position - 1].column;

    std::unique_ptr<ImportStmt> first;
    do {
        auto stmt = std::make_unique<ImportStmt>();
        stmt->line = line;
        stmt->column = column;
        stmt->module = parseModulePath();
        if (match(TokenType::AS)) {
            stmt->alias = consume(TokenType::IDENTIFIER, "Expected alias name after 'as'").value;
        }
        if (!first) first = std::move(stmt);
        else pending_statements.push_back(std::move(stmt));
    } while (match(TokenType::COMMA));

    match(TokenType::NEWLINE);
    return first;
}

''',
    marker="PyCppParser::parseModulePath",
)

patch(
    "PyCpp/src/pycpp_parser.cpp",
    r'''            if (auto stmt = parseTopLevelStatement()) {
                program->statements.push_back(std::move(stmt));
                pycpp::debugLog() << "Successfully parsed statement " << statement_count << std::endl;
                statement_count++;
            }''',
    r'''            if (auto stmt = parseTopLevelStatement()) {
                program->statements.push_back(std::move(stmt));
                for (auto& extra : pending_statements) {      // import a, b
                    program->statements.push_back(std::move(extra));
                    statement_count++;
                }
                pending_statements.clear();
                pycpp::debugLog() << "Successfully parsed statement " << statement_count << std::endl;
                statement_count++;
            }''',
    marker="for (auto& extra : pending_statements)",
)
patch(
    "PyCpp/src/pycpp_parser.cpp",
    r'''            reportError(e.what(), peek().line, peek().column);
            synchronize();
            // Don't break, try to continue parsing''',
    r'''            reportError(e.what(), peek().line, peek().column);
            pending_statements.clear();
            synchronize();
            // Don't break, try to continue parsing''',
    marker="pending_statements.clear();\n            synchronize();",
)

# ------------------------------------------------- semantic_analyzer.h
patch(
    "PyCpp/include/semantic_analyzer.h",
    "    void analyzeImportStmt(ImportStmt* import);\n",
    "    void analyzeImportStmt(ImportStmt* import);\n"
    "    void bindModuleName(const std::string& name, ImportStmt* import);\n",
    marker="bindModuleName(const std::string& name, ImportStmt* import);",
)

# ----------------------------------------------- semantic_analyzer.cpp
patch_between(
    "PyCpp/src/semantic_analyzer.cpp",
    "// ===== IMPORT HANDLING =====\n",
    "// Analysis Methods\n",
    r'''// ===== IMPORT HANDLING =====

// `import graphics` and `import graphics.shapes` both bind `graphics`: merge, don't collide.
void SemanticAnalyzer::bindModuleName(const std::string& name, ImportStmt* import) {
    auto& scope = scopes.back();
    auto it = scope.find(name);
    if (it != scope.end()) {
        if (it->second.type == "module") return;   // same namespace, nothing to add
        error("'" + name + "' conflicts with an existing declaration", import->line, import->column,
              "use `import ... as <other name>` to bind the module under a different name");
    }
    addSymbol(name, "module", true);
}

void SemanticAnalyzer::analyzeImportStmt(ImportStmt* import) {
    pycpp::debugLog() << "Processing import: " << import->module << std::endl;

    // `import a.b.c` binds `a`; `import a.b.c as x` binds `x`; `from a.b import n` binds only n.
    if (!import->is_from) {
        const std::string bound = !import->alias.empty()
            ? import->alias
            : import->module.substr(0, import->module.find('.'));
        bindModuleName(bound, import);
    }

    for (const auto& item : import->imports) {
        auto& scope = scopes.back();
        auto it = scope.find(item);
        if (it != scope.end() && it->second.type == "imported_symbol") continue;  // imported twice
        addSymbol(item, "imported_symbol", true);
        pycpp::debugLog() << "Added imported symbol: " << item << std::endl;
    }
}

''',
    marker="SemanticAnalyzer::bindModuleName",
)

# ------------------------------------------------------ code_generator.h
patch(
    "PyCpp/include/code_generator.h",
    "    bool isPointerExpr(Expr* expr);\n",
    "    bool isPointerExpr(Expr* expr);\n"
    "    bool isModuleExpr(Expr* expr);   // graphics, graphics.shapes, or an alias\n",
    marker="bool isModuleExpr(Expr* expr);",
)

# ---------------------------------------------------- code_generator.cpp
CG = "PyCpp/src/code_generator.cpp"

patch(
    CG,
    '        include_manager.trackManualInclude("\\"" + node.module + ".h\\"");',
    '        include_manager.trackManualInclude("\\"" + node.leaf() + ".h\\"");',
    marker='node.leaf() + ".h',
)

patch(
    CG,
    "CodeGenerator::CodeGenerator() : indent_level(0), current_function(nullptr), current_class(nullptr) {",
    r'''// graphics.shapes (MemberAccess chain) -> "graphics.shapes"; "" if it is not a pure name chain
static std::string dottedName(Expr* e) {
    if (auto id = dynamic_cast<Identifier*>(e)) return id->name;
    if (auto m = dynamic_cast<MemberAccess*>(e)) {
        std::string base = dottedName(m->object.get());
        return base.empty() ? std::string() : base + "." + m->member;
    }
    return "";
}

// True for `graphics`, `graphics.shapes` or an `as` alias, unless a local variable shadows it.
bool CodeGenerator::isModuleExpr(Expr* expr) {
    const std::string name = dottedName(expr);
    if (name.empty() || name == "self" || name == "this" || !imported_modules.count(name)) return false;
    return lookupVar(name.substr(0, name.find('.'))) == nullptr;
}

CodeGenerator::CodeGenerator() : indent_level(0), current_function(nullptr), current_class(nullptr) {''',
    marker="bool CodeGenerator::isModuleExpr",
)

patch(
    CG,
    r'''        if (auto import_stmt = dynamic_cast<ImportStmt*>(stmt.get())) {
            import_includes.push_back("\"" + import_stmt->module + ".h\"");
            imported_modules.insert(import_stmt->module);
        }''',
    r'''        if (auto import_stmt = dynamic_cast<ImportStmt*>(stmt.get())) {
            import_includes.push_back("\"" + import_stmt->leaf() + ".h\"");   // graphics.shapes -> "shapes.h"
            if (!import_stmt->is_from) {
                if (!import_stmt->alias.empty()) {
                    imported_modules.insert(import_stmt->alias);
                }
                else {
                    // import a.b.c makes a, a.b and a.b.c valid module references
                    std::string prefix;
                    std::istringstream parts(import_stmt->module);
                    for (std::string seg; std::getline(parts, seg, '.');) {
                        prefix += (prefix.empty() ? "" : ".") + seg;
                        imported_modules.insert(prefix);
                    }
                }
            }
        }''',
    marker="import a.b.c makes a, a.b and a.b.c",
)

patch(
    CG,
    r'''    for (const auto& include : import_includes) {
        output << "#include " << include << "\n";
    }''',
    r'''    for (const auto& include : import_includes) {
        if (include_manager.isHeaderIncluded(include)) continue;  // already emitted above
        output << "#include " << include << "\n";
    }''',
    marker="already emitted above",
)

patch(
    CG,
    r'''        if (auto module_member = dynamic_cast<MemberAccess*>(call_expr->callee.get())) {
            if (auto module_ident = dynamic_cast<Identifier*>(module_member->object.get())) {
                if (imported_modules.count(module_ident->name) > 0 && !lookupVar(module_ident->name)) {
                    output << module_member->member << "(";
                    for (size_t i = 0; i < call_expr->arguments.size(); ++i) {
                        if (i > 0) output << ", ";
                        generateExpression(call_expr->arguments[i].get());
                    }
                    output << ")";
                    return;
                }
            }
        }''',
    r'''        if (auto module_member = dynamic_cast<MemberAccess*>(call_expr->callee.get())) {
            if (isModuleExpr(module_member->object.get())) {
                output << module_member->member << "(";
                for (size_t i = 0; i < call_expr->arguments.size(); ++i) {
                    if (i > 0) output << ", ";
                    generateExpression(call_expr->arguments[i].get());
                }
                output << ")";
                return;
            }
        }''',
    marker="if (isModuleExpr(module_member->object.get()))",
)

patch(
    CG,
    r'''        // Check if this is module access (like greetings.hello)
        if (auto ident = dynamic_cast<Identifier*>(member_access->object.get())) {
            // Only identifiers that name an imported module are module accesses;
            // anything else (e.g. a local variable 'player') is an object access
            if (ident->name != "self" && ident->name != "this" &&
                imported_modules.count(ident->name) > 0 &&
                lookupVar(ident->name) == nullptr) {
                // This is likely a module access like greetings.hello
                // For now, generate as direct function call since we include the header
                // In a more advanced implementation, we might use namespaces
                output << member_access->member;
                return; // Early return to avoid the dot operator
            }
        }''',
    r'''        // Module access (greetings.hello, graphics.shapes.PI, alias.fn): the generated
        // headers declare plain functions, so only the member name is emitted.
        if (isModuleExpr(member_access->object.get())) {
            output << member_access->member;
            return;
        }''',
    marker="Module access (greetings.hello, graphics.shapes.PI",
)

# ---------------------------------------------------------------- write
if failures:
    print("\nNothing was written. Problems:\n")
    for f in failures:
        print(" -", f, "\n")
    sys.exit(1)

for rel, (text, crlf, bom) in files.items():
    if crlf:
        text = text.replace("\n", "\r\n")
    data = text.encode("utf-8")
    if bom:
        data = b"\xef\xbb\xbf" + data
    (ROOT / rel).write_bytes(data)

print("\nDone. Rebuild the compiler, then try:")
print("  import graphics.shapes as shapes / import a, b / import g + import g.s")