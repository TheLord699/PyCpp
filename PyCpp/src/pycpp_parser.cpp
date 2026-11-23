#include <cctype>
#include <limits>
#include "../include/pycpp_parser.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <unordered_map>

// ===== PyCppParser Implementation =====

PyCppParser::PyCppParser(const std::vector<Token>& tokens)
    : tokens(tokens), position(0), current_indent(0) {
    // Remember which functions/classes are generic ("def f[T]", "class C[T]") so that
    // Name[int](...) can be told apart from an ordinary subscript a[i].
    for (size_t i = 0; i + 2 < this->tokens.size(); ++i) {
        if ((this->tokens[i].type == TokenType::DEF || this->tokens[i].type == TokenType::CLASS) &&
            this->tokens[i + 1].type == TokenType::IDENTIFIER &&
            this->tokens[i + 2].type == TokenType::LBRACKET) {
            generic_names.insert(this->tokens[i + 1].value);
        }
    }
}

// [T, U] after a function or class name
std::vector<std::string> PyCppParser::parseTypeParams() {
    std::vector<std::string> params;
    if (!match(TokenType::LBRACKET)) return params;
    do {
        params.push_back(consume(TokenType::IDENTIFIER, "Expected generic parameter name").value);
    } while (match(TokenType::COMMA));
    consume(TokenType::RBRACKET, "Expected ']' after generic parameters");
    return params;
}

// [int, str] in an expression: Container[int](42), mem.alloc[Player]()
std::vector<std::unique_ptr<Type>> PyCppParser::parseTypeArgs() {
    std::vector<std::unique_ptr<Type>> args;
    consume(TokenType::LBRACKET, "Expected '[' before type arguments");
    do {
        args.push_back(parseType());
    } while (match(TokenType::COMMA));
    consume(TokenType::RBRACKET, "Expected ']' after type arguments");
    return args;
}

// After '.' or '::' any word is a member/C++ name, even one PyCPP lexes as a keyword
// (mem.free, list.append, std::sqrt, ...).
static bool isWordToken(const Token& token) {
    return !token.value.empty() &&
        (std::isalpha(static_cast<unsigned char>(token.value[0])) || token.value[0] == '_');
}

// A parser message can carry its own help text after a '\x1f' separator.
static std::string withHint(const std::string& message, const std::string& hint) {
    return message + '\x1f' + hint;
}

// [diag-fix] help text for the most common syntax errors
static std::string syntaxHint(const std::string& m) {
    auto has = [&](const char* s) { return m.find(s) != std::string::npos; };
    if (has("after the end of the statement"))
        return "put each statement on its own line (PyCPP has no ';' separator)";
    if (has("expected ':'"))
        return "block headers such as `def`, `if`, `for`, `while` and `class` must end with ':'";
    if (has("indented block") || has("indented class body") || has("indented struct body") || has("indented 'case'"))
        return "indent the body by 4 spaces";
    if (has("unexpected indentation"))
        return "this line is indented more than the block it belongs to; align it with the lines above";
    if (has("inconsistent indentation"))
        return "use the same number of spaces for every line of a block (a tab counts as 4 spaces)";
    if (has("unterminated string"))
        return "add the closing quote before the end of the line";
    if (has("expected ')'"))
        return "a '(' is probably not closed";
    if (has("expected ']'"))
        return "a '[' is probably not closed";
    if (has("expected '}'"))
        return "a '{' is probably not closed";
    if (has("expression"))
        return "a value, name or call is needed here";
    if (has("type specification"))
        return "write a type such as int, float, str, bool, list[int], *Node or a class name";
    if (has("function name") || has("class name") || has("struct name"))
        return "a name must follow the keyword";
    if (has("out of range"))
        return "use a smaller literal, or a float";
    if (has("not supported yet"))
        return "this Python feature is not implemented in PyCPP; see the README for the supported syntax";
    if (has("chained comparisons"))
        return "write each comparison separately: `a < b and b < c`";
    if (has("invalid assignment target"))
        return "only a variable, a member (a.b), an element (a[i]) or a dereference (*p) can be assigned to";
    if (has("unexpected character"))
        return "this character is not part of PyCPP syntax";
    return "";
}

// Stable code (error_codes.h) of a syntax error, from its message.
static const char* syntaxCode(const std::string& m) {
    auto has = [&](const char* s) { return m.find(s) != std::string::npos; };
    if (has("f-string")) return "E0014";
    if (has("unexpected character")) return "E0001";
    if (has("unterminated string")) return "E0002";
    if (has("expected ':'")) return "E0003";
    if (has("indented block") || has("indented class body") || has("indented struct body") ||
        has("indented 'case'") || has("unexpected indentation") || has("inconsistent indentation")) return "E0004";
    if (has("after the end of the statement")) return "E0005";
    if (has("expected ')'") || has("expected ']'") || has("expected '}'")) return "E0007";
    if (has("invalid assignment target")) return "E0008";
    if (has("chained comparisons")) return "E0009";
    if (has("out of range") || has("too large for 64 bits")) return "E0010";
    if (has("not supported")) return "E0011";
    if (has("type specification")) return "E0012";
    if (has("expression")) return "E0006";
    return "E0013";
}

void PyCppParser::reportError(const std::string& message, int line, int column) {
    // [diag-fix] structured, Rust-style syntax diagnostics
    pycpp::Diagnostic d;
    std::string text = message;
    std::string own_hint;
    const size_t sep = text.find('\x1f');               // message carries its own help text
    if (sep != std::string::npos) { own_hint = text.substr(sep + 1); text.erase(sep); }
    const size_t at = text.find(" at line ");
    if (at != std::string::npos) text.erase(at);               // the frame shows the location
    const size_t eof = text.find(" (reached end of file)");
    if (eof != std::string::npos) { text.erase(eof); d.label = "the file ends here"; }
    if (text.size() > 1 && std::isupper(static_cast<unsigned char>(text[0])) &&
        std::islower(static_cast<unsigned char>(text[1])))
        text[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[0])));

    const auto& src_lines = pycpp::source().lines;
    if (line <= 0 && !tokens.empty()) { line = tokens.back().line; column = tokens.back().column; }
    if (!src_lines.empty() && line > static_cast<int>(src_lines.size())) {
        line = static_cast<int>(src_lines.size());
        column = static_cast<int>(src_lines.back().size()) + 1;
    }
    for (const auto& t : tokens) {                               // underline the whole token
        if (t.line != line || t.column != column || t.value.empty() || t.type == TokenType::NEWLINE) continue;
        if (t.type != TokenType::UNKNOWN && t.end_line == t.line && t.end_column > t.column)
            d.length = t.end_column - t.column;
        break;
    }
    d.code = syntaxCode(text);
    if (d.code == "E0002" && line >= 1 && line <= static_cast<int>(src_lines.size())) {
        // the string runs on to the end of the line
        const int len = static_cast<int>(src_lines[line - 1].size()) - (column - 1);
        if (len > 0) d.length = len;
        d.label = "this string is never closed";
    }
    if (text.find("after the end of the statement") != std::string::npos)
        d.label = "expected a new line before this";
    d.message = text;
    d.line = line;
    d.column = column;
    d.hint = own_hint.empty() ? syntaxHint(text) : own_hint;
    for (const auto& e : errors)
        if (e.line == d.line && e.column == d.column && e.message == d.message) return;
    errors.push_back(std::move(d));
}

std::unique_ptr<Program> PyCppParser::parse() {
    auto program = parseProgram();
    if (!errors.empty()) {
        throw pycpp::CompileError(errors);
    }
    return program;
}

// ===== Helper Methods =====
bool PyCppParser::isMemberAccess() {
    if (!check(TokenType::IDENTIFIER)) return false;

    size_t save_pos = position;
    bool result = false;
    int dot_count = 0;

    while (check(TokenType::IDENTIFIER)) {
        advance();
        if (check(TokenType::DOT)) {
            advance();
            dot_count++;
            if (!check(TokenType::IDENTIFIER)) {
                result = false;
                break;
            }
        }
        else {
            if (position < tokens.size() && tokens[position].type == TokenType::COLON) {
                result = (dot_count > 0);
            }
            break;
        }
    }

    position = save_pos;
    return result;
}

// A variable, parameter or loop-variable name. Names of built-in functions and string
// methods (title, upper, count, max, ...) are lexed as keywords but may be used as names,
// as in Python.
std::string PyCppParser::consumeName(const std::string& error_msg) {
    if (check(TokenType::IDENTIFIER) || (!atEnd() && isBuiltInFunctionToken(peek().type))) return advance().value;
    return consume(TokenType::IDENTIFIER, error_msg).value;
}

bool PyCppParser::isBuiltInFunctionToken(TokenType type) {
    return type == TokenType::PRINT ||
        type == TokenType::INPUT ||
        type == TokenType::LEN ||
        type == TokenType::RANGE ||
        type == TokenType::ENUMERATE ||
        type == TokenType::ZIP ||
        type == TokenType::MAP_FUNC ||
        type == TokenType::FILTER ||
        type == TokenType::REDUCE ||
        type == TokenType::SORTED ||
        type == TokenType::REVERSED ||
        type == TokenType::MIN ||
        type == TokenType::MAX ||
        type == TokenType::SUM ||
        type == TokenType::ABS ||
        type == TokenType::ROUND ||
        type == TokenType::POW ||
        type == TokenType::DIVMOD ||
        type == TokenType::BIN ||
        type == TokenType::OCT ||
        type == TokenType::HEX ||
        type == TokenType::CHR ||
        type == TokenType::ORD ||
        type == TokenType::TYPE_FUNC ||
        type == TokenType::ISINSTANCE ||
        type == TokenType::ISSUBCLASS ||
        type == TokenType::HASATTR ||
        type == TokenType::GETATTR ||
        type == TokenType::SETATTR ||
        type == TokenType::DELATTR ||
        type == TokenType::PROPERTY ||
        type == TokenType::STATICMETHOD ||
        type == TokenType::CLASSMETHOD ||
        type == TokenType::INT_FUNC ||
        type == TokenType::FLOAT_FUNC ||
        type == TokenType::STR_FUNC ||
        type == TokenType::BOOL_FUNC ||
        type == TokenType::LIST_FUNC ||
        type == TokenType::DICT_FUNC ||
        type == TokenType::TUPLE_FUNC ||
        type == TokenType::SET_FUNC ||
        type == TokenType::FORMAT ||
        type == TokenType::JOIN ||
        type == TokenType::SPLIT ||
        type == TokenType::STRIP ||
        type == TokenType::REPLACE ||
        type == TokenType::FIND ||
        type == TokenType::STARTSWITH ||
        type == TokenType::ENDSWITH ||
        type == TokenType::LOWER ||
        type == TokenType::UPPER ||
        type == TokenType::CAPITALIZE ||
        type == TokenType::TITLE ||
        type == TokenType::OPEN ||
        type == TokenType::READ ||
        type == TokenType::WRITE ||
        type == TokenType::CLOSE ||
        type == TokenType::SEEK ||
        type == TokenType::TELL ||
        type == TokenType::MATH ||
        type == TokenType::SQRT ||
        type == TokenType::SIN ||
        type == TokenType::COS ||
        type == TokenType::TAN ||
        type == TokenType::LOG ||
        type == TokenType::LOG10 ||
        type == TokenType::EXP ||
        type == TokenType::CEIL ||
        type == TokenType::FLOOR ||
        type == TokenType::RANDOM ||
        type == TokenType::RANDINT ||
        type == TokenType::CHOICE ||
        type == TokenType::SHUFFLE ||
        type == TokenType::UNIFORM ||
        type == TokenType::EXIT ||
        type == TokenType::QUIT ||
        type == TokenType::HELP ||
        type == TokenType::DIR ||
        type == TokenType::VARS ||
        type == TokenType::LOCALS ||
        type == TokenType::GLOBALS ||
        type == TokenType::ID ||
        type == TokenType::HASH ||
        type == TokenType::DEL ||
        type == TokenType::GC ||
        type == TokenType::MALLOC ||
        type == TokenType::FREE;
}

bool PyCppParser::isMemberAccessDeclaration() {
    if (!check(TokenType::IDENTIFIER)) return false;

    size_t save_pos = position;
    bool result = false;

    // Look for pattern: identifier . identifier : 
    while (check(TokenType::IDENTIFIER)) {
        advance();
        if (check(TokenType::DOT)) {
            advance(); // consume dot
            // After a dot, we must have another identifier
            if (!check(TokenType::IDENTIFIER)) {
                result = false;
                break;
            }
        }
        else {
            // We've reached the end of the chain, check if next token is colon
            if (position < tokens.size() && tokens[position].type == TokenType::COLON) {
                result = true; // Found member access declaration pattern
            }
            break;
        }
    }

    position = save_pos; // Restore position - no tokens consumed
    return result;
}

bool PyCppParser::isValidAssignmentTarget(Expr* expr) {
    if (dynamic_cast<Identifier*>(expr)) return true;

    // Allow member access (obj.attr)
    if (auto member_access = dynamic_cast<MemberAccess*>(expr)) {
        return true;
    }

    // Pointer dereference as a target: *p = value, *p += 1
    if (auto unary = dynamic_cast<UnaryOp*>(expr)) {
        return unary->op == "*";
    }

    // Allow array subscript (arr[index])
    if (auto call = dynamic_cast<CallExpr*>(expr)) {
        if (call->is_subscript) {
            return true; // a[i], obj.items[i], a[i][j]   [fix2]
        }
    }

    return false;
}



Token PyCppParser::peek() {
    if (atEnd()) return Token(TokenType::END_OF_FILE, "", 0, 0);
    return tokens[position];
}

Token PyCppParser::advance() {
    if (atEnd()) {
        return Token(TokenType::END_OF_FILE, "", 0, 0);
    }
    if (position >= tokens.size()) {
        throw std::runtime_error("Parser position out of bounds: " +
            std::to_string(position) + " >= " +
            std::to_string(tokens.size()));
    }

    Token token = tokens[position];
    pycpp::debugLog() << "Advancing from " << position << " '" << token.value << "' to ";
    position++;

    if (!atEnd()) {
        pycpp::debugLog() << position << " '" << peek().value << "'" << std::endl;
    }
    else {
        pycpp::debugLog() << position << " (END)" << std::endl;
    }

    return token;
}

bool PyCppParser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool PyCppParser::check(TokenType type) {
    if (atEnd()) return false;
    return peek().type == type;
}

bool PyCppParser::checkNext(TokenType type) {
    if (position + 1 >= tokens.size()) return false;
    return tokens[position + 1].type == type;
}

Token PyCppParser::consume(TokenType type, const std::string& error_msg) {
    if (atEnd()) {
        throw std::runtime_error(error_msg + " (reached end of file)");
    }
    if (check(type)) return advance();

    throw std::runtime_error(error_msg + " at line " +
        std::to_string(peek().line) + ":" +
        std::to_string(peek().column));
}

bool PyCppParser::atEnd() {
    return position >= tokens.size() ||
        tokens[position].type == TokenType::END_OF_FILE;
}

void PyCppParser::synchronize() {
    if (atEnd()) return;

    pycpp::debugLog() << "Synchronizing from error at position " << position
        << ", token: '" << peek().value << "'" << std::endl;

    advance(); // Skip the problematic token

    while (!atEnd()) {
        if (tokens[position - 1].type == TokenType::NEWLINE) {
            pycpp::debugLog() << "Synchronized at newline" << std::endl;
            return;
        }

        switch (peek().type) {
        case TokenType::DEF:
        case TokenType::CLASS:
        case TokenType::IF:
        case TokenType::WHILE:
        case TokenType::FOR:
        case TokenType::RETURN:
        case TokenType::IMPORT:
        case TokenType::STRUCT:
        case TokenType::TEMPLATE:
            pycpp::debugLog() << "Synchronized at statement start: " << peek().value << std::endl;
            return;
        default:
            break;
        }
        advance();
    }

    pycpp::debugLog() << "Synchronized at end of file" << std::endl;
}

// ===== Indentation Handling =====

void PyCppParser::handleIndentation() {
    while (match(TokenType::NEWLINE)) {
        // Skip empty lines
    }
}

// ===== Top-Level Parsing =====

std::unique_ptr<Program> PyCppParser::parseProgram() {
    auto program = std::make_unique<Program>();
    program->line = 1;
    program->column = 1;

    pycpp::debugLog() << "Starting to parse program with " << tokens.size() << " tokens..." << std::endl;

    size_t last_position = position;
    size_t no_progress_count = 0;
    size_t statement_count = 0;

    while (!atEnd()) {
        // Prevent infinite loops
        if (position == last_position) {
            no_progress_count++;
            if (no_progress_count > 5) {
                reportError("could not parse near '" + peek().value + "'", peek().line, peek().column);
                break;
            }
        }
        else {
            last_position = position;
            no_progress_count = 0;
        }

        try {
            handleIndentation();
            pycpp::debugLog() << "=== Parsing top-level statement " << statement_count << " at position " << position << " ===" << std::endl;

            if (auto stmt = parseTopLevelStatement()) {
                program->statements.push_back(std::move(stmt));
                for (auto& extra : pending_statements) {      // import a, b
                    program->statements.push_back(std::move(extra));
                    statement_count++;
                }
                pending_statements.clear();
                pycpp::debugLog() << "Successfully parsed statement " << statement_count << std::endl;
                statement_count++;
            }
            else {
                pycpp::debugLog() << "No statement parsed, reached position: " << position << std::endl;
                break;
            }
        }
        catch (const std::runtime_error& e) {
            reportError(e.what(), peek().line, peek().column);
            pending_statements.clear();
            synchronize();
            if (check(TokenType::INDENT)) skipIndentedRegion();   // skip the body of the broken header
            // Don't break, try to continue parsing
        }
    }

    pycpp::debugLog() << "Finished parsing program with " << program->statements.size() << " statements" << std::endl;
    return program;
}

std::unique_ptr<Stmt> PyCppParser::parseTopLevelStatement() {
    if (atEnd()) return nullptr;

    pycpp::debugLog() << "parseTopLevelStatement at position " << position << ", token: '" << peek().value << "'" << std::endl;

    // Handle struct definitions FIRST (before other statements)
    if (check(TokenType::STRUCT)) {
        advance();
        return parseStructDef();
    }

    // Handle use statements
    if (check(TokenType::USE)) {
        advance();
        return parseUseStmt();
    }

    // Handle imports
    if (check(TokenType::IMPORT)) {
        advance();
        return parseImportStmt();
    }
    if (check(TokenType::FROM)) {
        advance();
        return parseFromImportStmt();
    }

    // Handle template definitions
    if (check(TokenType::TEMPLATE)) {
        advance();
        return parseTemplateDef();
    }

    // Handle constexpr
    if (check(TokenType::CONSTEXPR)) {
        advance();
        return parseConstexpr();
    }

    // Handle unsafe blocks
    if (check(TokenType::UNSAFE)) {
        advance();
        return parseUnsafeBlock();
    }

    // Standard statements
    if (check(TokenType::DEF)) {
        advance();
        return parseFunctionDef();
    }
    if (check(TokenType::CLASS)) {
        advance();
        return parseClassDef();
    }

    auto stmt = parseStatement();
    if (stmt) {
        pycpp::debugLog() << "Parsed regular statement at top level" << std::endl;
    }
    return stmt;
}
// [diag-fix] A statement must end at a newline (or at the end of its block).
void PyCppParser::expectStatementEnd() {
    if (match(TokenType::NEWLINE)) return;
    if (atEnd() || check(TokenType::DEDENT)) return;
    const Token t = peek();
    if (t.type == TokenType::UNKNOWN && t.value.size() > 1) throw std::runtime_error(t.value);
    throw std::runtime_error("unexpected '" + t.value + "' after the end of the statement");
}

std::unique_ptr<Stmt> PyCppParser::parseStatement() {
    // Handle unsafe blocks in statement context
    if (check(TokenType::UNSAFE)) {
        advance();
        return parseUnsafeBlock();
    }

    // '...' placeholder statement
    if (check(TokenType::ELLIPSIS)) {
        auto placeholder = std::make_unique<PassStmt>();
        placeholder->is_ellipsis = true;
        placeholder->line = peek().line;
        placeholder->column = peek().column;
        advance();
        match(TokenType::NEWLINE);
        return placeholder;
    }

    if (check(TokenType::DEFER)) { advance(); return parseDeferStmt(); }
    if (check(TokenType::TRY)) { advance(); return parseTryStmt(); }
    if (check(TokenType::RAISE)) { advance(); return parseRaiseStmt(); }
    if (check(TokenType::MATCH)) { advance(); return parseMatchStmt(); }

    // assert cond[, "message"]
    if (check(TokenType::ASSERT)) {
        return parseAssertStmt();
    }

    // Control flow statements
    if (check(TokenType::IF)) {
        advance();
        return parseIfStmt();
    }
    if (check(TokenType::WHILE)) {
        advance();
        return parseWhileStmt();
    }
    if (check(TokenType::FOR)) {
        advance();
        return parseForStmt();
    }
    if (check(TokenType::RETURN)) {
        advance();
        return parseReturnStmt();
    }
    if (check(TokenType::PASS)) {
        advance();
        return parsePassStmt();
    }
    if (check(TokenType::BREAK)) {
        advance();
        return parseBreakStmt();
    }
    if (check(TokenType::CONTINUE)) {
        advance();
        return parseContinueStmt();
    }

    // Check for variable declaration patterns FIRST (before expressions)
    // This is critical because once tokens are consumed by expression parsing, 
    // we can't go back and reinterpret them as declarations

    // Look for simple variable declaration: identifier : type
    if ((check(TokenType::IDENTIFIER) || isBuiltInFunctionToken(peek().type)) && checkNext(TokenType::COLON)) {
        return parseVarDecl();
    }

    // Look for member variable declaration: identifier . identifier : type
    if (isMemberAccessDeclaration()) {
        return parseVarDecl();
    }

    // Only if it's not a declaration, try to parse as expression
    if (auto expr = parseExpression()) {
        auto stmt = std::make_unique<ExprStmt>();
        const SourceSpan where = spanOf(expr.get());
        stmt->expr = std::move(expr);
        stmt->line = where.line > 0 ? where.line : tokens[position].line;
        stmt->column = where.line > 0 ? where.column : tokens[position].column;

        expectStatementEnd();
        return stmt;
    }

    return nullptr;
}

// ===== Function and Class Parsing =====

std::unique_ptr<FunctionDef> PyCppParser::parseFunctionDef() {
    auto func = std::make_unique<FunctionDef>();
    func->line = tokens[position - 1].line;
    func->column = tokens[position - 1].column;

    // Special methods (__init__, __str__, ...) are lexed as dedicated keyword
    // tokens rather than IDENTIFIER, but they are valid function names after 'def'.
    if (check(TokenType::IDENTIFIER) ||
        (peek().value.size() > 4 && peek().value.compare(0, 2, "__") == 0 &&
         peek().value.compare(peek().value.size() - 2, 2, "__") == 0)) {
        func->name = advance().value;
    }
    else {
        throw std::runtime_error("Expected function name at line " +
            std::to_string(peek().line) + ":" + std::to_string(peek().column));
    }
    func->type_params = parseTypeParams(); // def f[T](...)
    pycpp::debugLog() << "Parsing function: " << func->name << std::endl;

    consume(TokenType::LPAREN, "Expected '(' after function name");

    // Parse parameters
    if (!check(TokenType::RPAREN)) {
        do {
            std::string param_name = consumeName("Expected parameter name");
            pycpp::debugLog() << "Found parameter: " << param_name << std::endl;

            // Handle parameters with type annotations
            std::unique_ptr<Type> param_type = nullptr;
            std::unique_ptr<Expr> default_value = nullptr;

            if (match(TokenType::COLON)) {
                param_type = parseType();
                pycpp::debugLog() << "Parameter " << param_name << " has type: " << (param_type ? param_type->name : "null") << std::endl;
            }
            else {
                // For 'self' parameter in methods, create a basic type
                if (param_name == "self") {
                    auto self_type = std::make_unique<BasicType>();
                    self_type->name = "auto"; // self is automatically typed
                    param_type = std::move(self_type);
                    pycpp::debugLog() << "Auto-typed self parameter" << std::endl;
                }
            }

            // Optional default value
            if (match(TokenType::EQUAL)) {
                default_value = parseExpression();
                pycpp::debugLog() << "Parameter " << param_name << " has default value" << std::endl;
            }

            func->parameters.emplace_back(param_name, std::move(param_type), std::move(default_value));
        } while (match(TokenType::COMMA));
    }

    consume(TokenType::RPAREN, "Expected ')' after parameters");

    // FIXED: Enhanced return type parsing
    if (match(TokenType::ARROW)) {
        // Handle special case for None -> void mapping
        if (check(TokenType::NONE)) {
            auto void_type = std::make_unique<BasicType>();
            void_type->name = "void";
            func->return_type = std::move(void_type);
            advance(); // consume 'None'
            pycpp::debugLog() << "Function has return type: void (from None)" << std::endl;
        }
        else {
            func->return_type = parseType();
            pycpp::debugLog() << "Function has return type: " << func->return_type->name << std::endl;
        }
    }
    else {
        auto void_type = std::make_unique<BasicType>();
        void_type->name = "void";
        func->return_type = std::move(void_type);
        pycpp::debugLog() << "Function has implicit void return type" << std::endl;
    }

    consume(TokenType::COLON, "Expected ':' before function body");

    // Handle function body
    if (check(TokenType::NEWLINE)) {
        // Multi-line function
        consume(TokenType::NEWLINE, "Expected newline after function header");
        pycpp::debugLog() << "Parsing multi-line function body for " << func->name << std::endl;
        func->body = parseBlock();
        pycpp::debugLog() << "Function " << func->name << " body has " << func->body.size() << " statements" << std::endl;
    }
    else {
        // Single-line function (inline)
        pycpp::debugLog() << "Parsing single-line function body for " << func->name << std::endl;
        if (auto stmt = parseStatement()) {
            func->body.push_back(std::move(stmt));
            pycpp::debugLog() << "Single-line function has 1 statement" << std::endl;
        }
    }

    pycpp::debugLog() << "Finished parsing function: " << func->name << std::endl;
    return func;
}

std::unique_ptr<ClassDef> PyCppParser::parseClassDef() {
    auto class_def = std::make_unique<ClassDef>();
    class_def->line = tokens[position - 1].line;
    class_def->column = tokens[position - 1].column;

    pycpp::debugLog() << "=== STARTING CLASS PARSING ===" << std::endl;
    pycpp::debugLog() << "Current position: " << position << ", token: '" << peek().value << "'" << std::endl;

    // Class name
    class_def->name = consume(TokenType::IDENTIFIER, "Expected class name").value;
    class_def->type_params = parseTypeParams(); // class C[T]:
    pycpp::debugLog() << "Class name: " << class_def->name << std::endl;

    // Inheritance (optional)
    if (match(TokenType::LPAREN)) {
        pycpp::debugLog() << "Found inheritance at position: " << position << std::endl;
        if (check(TokenType::IDENTIFIER)) {
            class_def->base_class = advance().value;
            pycpp::debugLog() << "Base class: " << class_def->base_class << std::endl;
        }
        consume(TokenType::RPAREN, "Expected ')' after base class");
    }

    // Class body
    pycpp::debugLog() << "Before colon, position: " << position << ", token: '" << peek().value << "'" << std::endl;
    consume(TokenType::COLON, "Expected ':' before class body");
    pycpp::debugLog() << "After colon, position: " << position << ", token: '" << peek().value << "'" << std::endl;

    if (check(TokenType::NEWLINE)) {
        consume(TokenType::NEWLINE, "Expected newline after class definition");
        pycpp::debugLog() << "After newline, position: " << position << ", token: '" << peek().value << "'" << std::endl;

        // The class body is an INDENT ... DEDENT block
        handleIndentation();
        if (!check(TokenType::INDENT)) {
            throw std::runtime_error("Expected an indented class body at line " +
                std::to_string(peek().line) + ":" + std::to_string(peek().column));
        }
        advance(); // consume INDENT

        int member_count = 0;

        while (!atEnd()) {
            const size_t member_start = position;
            pycpp::debugLog() << "=== Parsing class member " << member_count << " ===" << std::endl;
            pycpp::debugLog() << "Position: " << position << ", token: '" << peek().value << "' type: " << static_cast<int>(peek().type) << std::endl;

            handleIndentation();

            // The matching DEDENT ends the class body
            if (check(TokenType::DEDENT)) {
                advance();
                pycpp::debugLog() << "=== ENDING CLASS PARSING - dedent ===" << std::endl;
                break;
            }

            // Check for end of file
            if (atEnd()) {
                pycpp::debugLog() << "End of class detected (EOF) at position: " << position << std::endl;
                break;
            }

            if (check(TokenType::INDENT)) {
                reportError("unexpected indentation", peek().line, peek().column);
                skipIndentedRegion();
                continue;
            }

            try {
                // Parse class members (methods and attributes)
                if (check(TokenType::DEF)) {
                    pycpp::debugLog() << "Found method definition at position: " << position << std::endl;
                    size_t method_start = position;
                    advance(); // consume 'def'

                    try {
                        auto method = parseFunctionDef();
                        if (method) {
                            class_def->methods.push_back(std::move(method));
                            pycpp::debugLog() << "Successfully parsed method: " << class_def->methods.back()->name << std::endl;
                        }
                        else {
                            pycpp::debugLog() << "Failed to parse method" << std::endl;
                            position = method_start;
                            synchronizeInBlock();
                        }
                    }
                    catch (const std::runtime_error& e) {
                        reportError(e.what(), peek().line, peek().column);
                        position = method_start;
                        synchronizeInBlock();
                    }
                }
                else if (isClassAttributeDeclaration()) {
                    pycpp::debugLog() << "Found class attribute definition at position: " << position << std::endl;
                    auto attr = parseClassAttribute();
                    if (attr) {
                        class_def->attributes.push_back(std::move(attr));
                        pycpp::debugLog() << "Successfully parsed class attribute: " << class_def->attributes.back()->var_name << std::endl;
                    }
                    else {
                        pycpp::debugLog() << "Failed to parse class attribute" << std::endl;
                        synchronizeInBlock();
                    }
                }
                else {
                    // Try to parse as a statement (might be method body content)
                    pycpp::debugLog() << "Attempting to parse as statement in class context at position " << position << std::endl;
                    if (auto stmt = parseStatement()) {
                        pycpp::debugLog() << "Parsed statement in class context: " << typeid(*stmt).name() << std::endl;
                        // Note: This might indicate a parsing issue if we're getting non-member statements
                    }
                    else {
                        pycpp::debugLog() << "Unexpected token in class at position " << position
                            << ": '" << peek().value << "' type: " << static_cast<int>(peek().type) << std::endl;
                        synchronizeInBlock();
                    }
                }
            }
            catch (const std::runtime_error& e) {
                reportError(e.what(), peek().line, peek().column);
                synchronizeInBlock();
            }

            member_count++;
            if (position == member_start) {
                // No progress: report it and skip the token so the loop always ends
                reportError("unexpected '" + peek().value + "' in class body", peek().line, peek().column);
                advance();
            }
        }
    }
    else {
        pycpp::debugLog() << "No newline after class, single-line class?" << std::endl;
        // Handle single-line class (uncommon but possible)
        if (check(TokenType::DEF)) {
            pycpp::debugLog() << "Found method in single-line class" << std::endl;
            advance(); // consume 'def'
            auto method = parseFunctionDef();
            if (method) {
                class_def->methods.push_back(std::move(method));
            }
        }
        else if (isClassAttributeDeclaration()) {
            pycpp::debugLog() << "Found attribute in single-line class" << std::endl;
            auto attr = parseClassAttribute();
            if (attr) {
                class_def->attributes.push_back(std::move(attr));
            }
        }
    }

    pycpp::debugLog() << "=== FINISHED CLASS PARSING: " << class_def->name << " with "
        << class_def->methods.size() << " methods and "
        << class_def->attributes.size() << " attributes ===" << std::endl;
    return class_def;
}

// Helper function to detect class attribute declarations
bool PyCppParser::isClassAttributeDeclaration() {
    if (!check(TokenType::IDENTIFIER)) return false;

    size_t save_pos = position;
    bool result = false;

    // Look for patterns:
    // 1. self.member : type
    // 2. self.member : type = value
    // 3. member : type (in class context)

    while (check(TokenType::IDENTIFIER)) {
        std::string ident = advance().value;

        // Pattern 1: self.member
        if (ident == "self" && check(TokenType::DOT)) {
            advance(); // consume dot
            if (check(TokenType::IDENTIFIER)) {
                advance(); // consume member name
                if (check(TokenType::COLON)) {
                    result = true;
                    break;
                }
            }
            break;
        }

        // Pattern 2: direct member in class context
        if (check(TokenType::COLON)) {
            result = true;
            break;
        }

        // Check for dots in member chain
        if (check(TokenType::DOT)) {
            advance();
            continue;
        }

        break;
    }

    position = save_pos;
    return result;
}

// Parse class attribute (member variable)
std::unique_ptr<VarDecl> PyCppParser::parseClassAttribute() {
    auto var_decl = std::make_unique<VarDecl>();
    var_decl->line = peek().line;
    var_decl->column = peek().column;

    pycpp::debugLog() << "Starting to parse class attribute at position " << position << std::endl;

    // Variable name - could be a simple identifier or member access
    std::string member_name;

    if (isMemberAccess()) {
        pycpp::debugLog() << "Detected member access pattern for class attribute" << std::endl;
        // Handle member access like self.name or self.obj.attr
        while (check(TokenType::IDENTIFIER)) {
            if (!member_name.empty()) member_name += ".";
            member_name += advance().value;
            if (!match(TokenType::DOT)) break;
        }
        var_decl->var_name = member_name;
        pycpp::debugLog() << "Parsing class attribute: " << var_decl->var_name << std::endl;
    }
    else {
        // Simple identifier (without self. prefix)
        var_decl->var_name = consume(TokenType::IDENTIFIER, "Expected attribute name").value;
        pycpp::debugLog() << "Parsing simple class attribute: " << var_decl->var_name << std::endl;
    }

    // We should be at the colon now
    if (!check(TokenType::COLON)) {
        throw std::runtime_error("Expected ':' after attribute name '" + var_decl->var_name + "'");
    }
    advance(); // consume ':'
    pycpp::debugLog() << "Consumed colon for attribute: " << var_decl->var_name << std::endl;

    // Type
    var_decl->type = parseType();
    pycpp::debugLog() << "Attribute " << var_decl->var_name << " has type: " << var_decl->type->name << std::endl;

    // Optional initializer
    if (match(TokenType::EQUAL)) {
        pycpp::debugLog() << "Attribute " << var_decl->var_name << " has initializer" << std::endl;
        var_decl->value = parseExpression();
    }

    expectStatementEnd();

    pycpp::debugLog() << "Successfully parsed class attribute: " << var_decl->var_name << std::endl;
    return var_decl;
}

// ===== Struct Definition =====

std::unique_ptr<StructDef> PyCppParser::parseStructDef() {
    auto struct_def = std::make_unique<StructDef>();
    struct_def->line = tokens[position - 1].line;
    struct_def->column = tokens[position - 1].column;

    // Struct name
    struct_def->name = consume(TokenType::IDENTIFIER, "Expected struct name").value;

    // Struct body: an INDENT ... DEDENT block of typed fields
    consume(TokenType::COLON, "Expected ':' before struct body");
    consume(TokenType::NEWLINE, "Expected newline after struct definition");

    handleIndentation();
    if (!check(TokenType::INDENT)) {
        throw std::runtime_error("Expected an indented struct body at line " +
            std::to_string(peek().line) + ":" + std::to_string(peek().column));
    }
    advance(); // consume INDENT

    while (!atEnd()) {
        handleIndentation();

        if (check(TokenType::DEDENT)) {
            advance(); // end of struct body
            break;
        }

        if (atEnd()) {
            break;
        }

        if (check(TokenType::IDENTIFIER) && checkNext(TokenType::COLON)) {
            struct_def->fields.push_back(parseVarDecl());
        }
        else {
            throw std::runtime_error("Expected a field declaration in struct '" +
                struct_def->name + "' at line " + std::to_string(peek().line) +
                ":" + std::to_string(peek().column));
        }
    }

    return struct_def;
}

// ===== Template Definition =====

std::unique_ptr<TemplateDef> PyCppParser::parseTemplateDef() {
    auto template_def = std::make_unique<TemplateDef>();
    template_def->line = tokens[position - 1].line;
    template_def->column = tokens[position - 1].column;

    pycpp::debugLog() << "Parsing template definition" << std::endl;

    // Template parameters
    consume(TokenType::LESS, "Expected '<' after 'template'");

    // Handle 'typename' keyword
    if (check(TokenType::TYPENAME)) {
        advance(); // consume "typename"
        pycpp::debugLog() << "Found typename keyword" << std::endl;
    }

    template_def->type_param = consume(TokenType::IDENTIFIER, "Expected type parameter name").value;
    pycpp::debugLog() << "Template parameter: " << template_def->type_param << std::endl;

    consume(TokenType::GREATER, "Expected '>' after template parameters");

    // Skip any newlines after template declaration
    handleIndentation();

    // Parse the templated definition (class or function)
    if (check(TokenType::CLASS)) {
        advance(); // consume "class"
        pycpp::debugLog() << "Parsing templated class" << std::endl;
        template_def->templated_class = parseClassDef();
    }
    else if (check(TokenType::DEF)) {
        advance(); // consume "def"  
        pycpp::debugLog() << "Parsing templated function" << std::endl;
        template_def->templated_function = parseFunctionDef();
    }
    else {
        throw std::runtime_error("Expected 'class' or 'def' after template declaration, got: " + peek().value);
    }

    pycpp::debugLog() << "Finished parsing template" << std::endl;
    return template_def;
}

// ===== Constexpr =====

std::unique_ptr<ConstexprDef> PyCppParser::parseConstexpr() {
    auto constexpr_def = std::make_unique<ConstexprDef>();
    constexpr_def->line = tokens[position - 1].line;
    constexpr_def->column = tokens[position - 1].column;

    // Variable name and type
    constexpr_def->name = consume(TokenType::IDENTIFIER, "Expected constant name").value;
    consume(TokenType::COLON, "Expected ':' after constant name");
    constexpr_def->type = parseType();
    consume(TokenType::EQUAL, "Expected '=' in constexpr declaration");
    constexpr_def->value = parseExpression();

    match(TokenType::NEWLINE); // Optional newline

    return constexpr_def;
}

// ===== Unsafe Block =====

std::unique_ptr<UnsafeBlock> PyCppParser::parseUnsafeBlock() {
    auto unsafe_block = std::make_unique<UnsafeBlock>();
    unsafe_block->line = tokens[position - 1].line;
    unsafe_block->column = tokens[position - 1].column;

    consume(TokenType::COLON, "Expected ':' after 'unsafe'");

    if (check(TokenType::NEWLINE)) {
        // Multi-line unsafe block
        consume(TokenType::NEWLINE, "Expected newline after unsafe");
        unsafe_block->body = parseBlock();
    }
    else {
        // Single-line unsafe block
        if (auto stmt = parseStatement()) {
            unsafe_block->body.push_back(std::move(stmt));
        }
        else {
            throw std::runtime_error("Expected statement in unsafe block");
        }
    }

    return unsafe_block;
}

// ===== Variable Declaration =====

std::unique_ptr<VarDecl> PyCppParser::parseVarDecl() {
    auto var_decl = std::make_unique<VarDecl>();
    var_decl->line = peek().line;
    var_decl->column = peek().column;

    pycpp::debugLog() << "Starting to parse variable declaration at position " << position << std::endl;

    // Variable name - could be a simple identifier or member access
    std::string member_name;

    if (isMemberAccess()) {
        pycpp::debugLog() << "Detected member access pattern for variable declaration" << std::endl;
        // Handle member access like self.name or self.obj.attr
        while (check(TokenType::IDENTIFIER)) {
            if (!member_name.empty()) member_name += ".";
            member_name += advance().value;
            if (!match(TokenType::DOT)) break;
        }
        var_decl->var_name = member_name;
        pycpp::debugLog() << "Parsing member variable: " << var_decl->var_name << std::endl;
    }
    else {
        // Simple identifier
        var_decl->var_name = consumeName("Expected variable name");
        pycpp::debugLog() << "Parsing simple variable: " << var_decl->var_name << std::endl;
    }

    // We should be at the colon now
    if (!check(TokenType::COLON)) {
        throw std::runtime_error("Expected ':' after variable name '" + var_decl->var_name + "'");
    }
    advance(); // consume ':'
    pycpp::debugLog() << "Consumed colon for variable: " << var_decl->var_name << std::endl;

    // Type
    var_decl->type = parseType();
    pycpp::debugLog() << "Variable " << var_decl->var_name << " has type: " << var_decl->type->name << std::endl;

    // Optional initializer
    if (match(TokenType::EQUAL)) {
        pycpp::debugLog() << "Variable " << var_decl->var_name << " has initializer" << std::endl;
        var_decl->value = parseExpression();
    }

    expectStatementEnd();

    pycpp::debugLog() << "Successfully parsed variable declaration: " << var_decl->var_name << std::endl;
    return var_decl;
}

// ===== Type Parsing =====

std::unique_ptr<Type> PyCppParser::parseType() {
    // 'const' qualifier ('const' is not a keyword token, so it arrives as an identifier)
    bool is_const = false;
    if (check(TokenType::IDENTIFIER) && peek().value == "const") {
        advance();
        is_const = true;
    }

    // Reference types: &T (mutable) and const &T (immutable)
    if (match(TokenType::BITWISE_AND)) {
        auto referenced = parseType();
        referenced->is_reference = true;
        if (is_const) referenced->is_const = true;
        return referenced;
    }

    // Pointer types: *T and **T (the lexer reads '**' as one DOUBLE_STAR token)
    if (check(TokenType::STAR) || check(TokenType::DOUBLE_STAR)) {
        int levels = check(TokenType::DOUBLE_STAR) ? 2 : 1;
        int line = peek().line;
        int column = peek().column;
        advance();

        std::unique_ptr<Type> inner = parseType();
        for (int i = 0; i < levels; ++i) {
            auto ptr_type = std::make_unique<PointerType>();
            ptr_type->base_type = std::move(inner);
            ptr_type->name = "*" + ptr_type->base_type->name;
            ptr_type->line = line;
            ptr_type->column = column;
            inner = std::move(ptr_type);
        }
        if (is_const) inner->is_const = true;
        return inner;
    }

    auto finish = [&](std::unique_ptr<Type> t) {
        if (is_const) t->is_const = true;
        return t;
    };

    // Handle list types
    if (match(TokenType::LIST)) {
        auto list_type = std::make_unique<ListType>();
        list_type->line = tokens[position - 1].line;
        list_type->column = tokens[position - 1].column;

        consume(TokenType::LBRACKET, "Expected '[' after 'list'");
        list_type->element_type = parseType();
        consume(TokenType::RBRACKET, "Expected ']' after list element type");
        list_type->name = "std::vector<" + list_type->element_type->name + ">";
        return finish(std::move(list_type));
    }

    // set[T] -> std::set<T>
    if (check(TokenType::SET) && checkNext(TokenType::LBRACKET)) {
        const Token set_token = advance();
        consume(TokenType::LBRACKET, "Expected '[' after 'set'");
        auto element = parseType();
        consume(TokenType::RBRACKET, "Expected ']' after set element type");
        auto set_type = std::make_unique<BasicType>();
        set_type->line = set_token.line;
        set_type->column = set_token.column;
        set_type->name = "std::set<" + element->name + ">";
        return finish(std::move(set_type));
    }

    // Handle dict types
    if (match(TokenType::DICT)) {
        auto dict_type = std::make_unique<DictType>();
        dict_type->line = tokens[position - 1].line;
        dict_type->column = tokens[position - 1].column;

        consume(TokenType::LBRACKET, "Expected '[' after 'dict'");
        dict_type->key_type = parseType();
        consume(TokenType::COMMA, "Expected ',' between dict key and value types");
        dict_type->value_type = parseType();
        consume(TokenType::RBRACKET, "Expected ']' after dict value type");
        dict_type->name = "std::map<" + dict_type->key_type->name + "," + dict_type->value_type->name + ">";
        return finish(std::move(dict_type));
    }

    // FIXED: Enhanced type parsing to handle None and all basic types
    if (check(TokenType::IDENTIFIER) ||
        check(TokenType::INT) || check(TokenType::FLOAT) || check(TokenType::DOUBLE) ||
        check(TokenType::INT32) || check(TokenType::INT64) ||
        check(TokenType::FLOAT32) || check(TokenType::FLOAT64) || check(TokenType::STRING) ||
        check(TokenType::BOOL) || check(TokenType::STR) || check(TokenType::CHAR) ||
        check(TokenType::VOID) || check(TokenType::AUTO) || check(TokenType::NONE) ||
        check(TokenType::NONE_TYPE)) {

        Token type_token = advance();
        auto basic_type = std::make_unique<BasicType>();
        basic_type->line = type_token.line;
        basic_type->column = type_token.column;

        // Enhanced type mapping for PyCPP to C++
        static std::unordered_map<std::string, std::string> type_map = {
            {"int", "int"},
            {"i8", "int8_t"},
            {"i16", "int16_t"},
            {"i32", "int32_t"},
            {"i64", "int64_t"},
            {"float", "float"},
            {"f32", "float"},
            {"f64", "double"},
            {"double", "double"},
            {"bool", "bool"},
            {"str", "std::string"},
            {"string", "std::string"},  // Map both to std::string
            {"char", "char"},
            {"void", "void"},
            {"auto", "auto"},
            {"None", "void"},           // Map None to void
            {"NoneType", "void"}        // Map NoneType to void
        };

        auto it = type_map.find(type_token.value);
        if (it != type_map.end()) {
            basic_type->name = it->second;
        }
        else {
            basic_type->name = type_token.value; // User-defined type
        }

        // Generic arguments: Container[int], Pair[str, int]
        if (check(TokenType::LBRACKET)) {
            advance(); // '['
            do {
                basic_type->type_args.push_back(parseType());
            } while (match(TokenType::COMMA));
            consume(TokenType::RBRACKET, "Expected ']' after generic type arguments");
        }

        return finish(std::move(basic_type));
    }

    throw std::runtime_error("Expected type specification, got: " +
        (atEnd() ? "EOF" : "'" + peek().value + "'"));
}

// ===== Control Flow Statements =====

// FIXED: Enhanced if statement parsing
std::unique_ptr<IfStmt> PyCppParser::parseIfStmt() {
    auto if_stmt = std::make_unique<IfStmt>();
    if_stmt->line = tokens[position - 1].line;
    if_stmt->column = tokens[position - 1].column;

    // Condition
    if_stmt->condition = parseExpression();
    consume(TokenType::COLON, "Expected ':' after if condition");

    if (check(TokenType::NEWLINE)) {
        consume(TokenType::NEWLINE, "Expected newline after if statement");
        if_stmt->then_branch = parseBlock();
    }
    else {
        // Single-line if
        if (auto stmt = parseStatement()) {
            if_stmt->then_branch.push_back(std::move(stmt));
        }
    }

    // Elif branches
    while (check(TokenType::ELIF)) {
        advance(); // consume 'elif'
        auto elif_condition = parseExpression();
        consume(TokenType::COLON, "Expected ':' after elif condition");

        std::vector<std::unique_ptr<Stmt>> elif_body;
        if (check(TokenType::NEWLINE)) {
            consume(TokenType::NEWLINE, "Expected newline after elif statement");
            elif_body = parseBlock();
        }
        else {
            // Single-line elif
            if (auto stmt = parseStatement()) {
                elif_body.push_back(std::move(stmt));
            }
        }
        if_stmt->elif_branches.emplace_back(std::move(elif_condition), std::move(elif_body));
    }

    // FIX: Only parse else branch if it's at the same indentation level as the original if
    if (check(TokenType::ELSE)) {
        // Save position to check if this else belongs to the current if
        size_t else_pos = position;
        advance(); // consume 'else'

        // Check if there's a colon immediately after else
        if (check(TokenType::COLON)) {
            consume(TokenType::COLON, "Expected ':' after else");

            if (check(TokenType::NEWLINE)) {
                consume(TokenType::NEWLINE, "Expected newline after else");
                if_stmt->else_branch = parseBlock();
            }
            else {
                // Single-line else
                if (auto stmt = parseStatement()) {
                    if_stmt->else_branch.push_back(std::move(stmt));
                }
            }
        }
        else {
            // This else doesn't have a colon, so it might belong to a different if
            // Roll back and let the outer if statement handle it
            position = else_pos;
        }
    }

    return if_stmt;
}
std::unique_ptr<WhileStmt> PyCppParser::parseWhileStmt() {
    auto while_stmt = std::make_unique<WhileStmt>();
    while_stmt->line = tokens[position - 1].line;
    while_stmt->column = tokens[position - 1].column;

    // Condition
    while_stmt->condition = parseExpression();
    consume(TokenType::COLON, "Expected ':' after while condition");

    if (check(TokenType::NEWLINE)) {
        consume(TokenType::NEWLINE, "Expected newline after while statement");
        while_stmt->body = parseBlock();
    }
    else {
        // Single-line while
        if (auto stmt = parseStatement()) {
            while_stmt->body.push_back(std::move(stmt));
        }
    }

    return while_stmt;
}

// FIXED: Enhanced for statement parsing
std::unique_ptr<ForStmt> PyCppParser::parseForStmt() {
    auto for_stmt = std::make_unique<ForStmt>();
    for_stmt->line = tokens[position - 1].line;
    for_stmt->column = tokens[position - 1].column;

    // Loop variable
    do {
        for_stmt->var_names.push_back(consumeName("Expected loop variable name"));
    } while (match(TokenType::COMMA));
    for_stmt->var_name = for_stmt->var_names.front();
    consume(TokenType::IN, "Expected 'in' after loop variable");

    // Iterable
    for_stmt->iterable = parseExpression();
    consume(TokenType::COLON, "Expected ':' after for statement");

    if (check(TokenType::NEWLINE)) {
        consume(TokenType::NEWLINE, "Expected newline after for statement");

        // FIXED: Set loop context for block parsing
        for_stmt->body = parseBlock();

        // FIXED: Ensure return statements are not inside the loop
        // Check if the last statement in the loop body is a return
        if (!for_stmt->body.empty()) {
            auto last_stmt = for_stmt->body.back().get();
            if (dynamic_cast<ReturnStmt*>(last_stmt)) {
                // If return is the last statement in loop, it's probably incorrect
                pycpp::debugLog() << "WARNING: Return statement found as last statement in for loop body" << std::endl;
            }
        }
    }
    else {
        // Single-line for
        if (auto stmt = parseStatement()) {
            for_stmt->body.push_back(std::move(stmt));
        }
    }

    return for_stmt;
}

std::unique_ptr<ReturnStmt> PyCppParser::parseReturnStmt() {
    auto return_stmt = std::make_unique<ReturnStmt>();
    return_stmt->line = tokens[position - 1].line;
    return_stmt->column = tokens[position - 1].column;

    // Return value (optional)
    if (!check(TokenType::NEWLINE) && !atEnd()) {
        return_stmt->value = parseExpression();
    }

    expectStatementEnd();

    return return_stmt;
}

std::unique_ptr<PassStmt> PyCppParser::parsePassStmt() {
    auto pass_stmt = std::make_unique<PassStmt>();
    pass_stmt->line = tokens[position - 1].line;
    pass_stmt->column = tokens[position - 1].column;
    match(TokenType::NEWLINE);
    return pass_stmt;
}

std::unique_ptr<BreakStmt> PyCppParser::parseBreakStmt() {
    auto break_stmt = std::make_unique<BreakStmt>();
    break_stmt->line = tokens[position - 1].line;
    break_stmt->column = tokens[position - 1].column;
    match(TokenType::NEWLINE);
    return break_stmt;
}

std::unique_ptr<ContinueStmt> PyCppParser::parseContinueStmt() {
    auto continue_stmt = std::make_unique<ContinueStmt>();
    continue_stmt->line = tokens[position - 1].line;
    continue_stmt->column = tokens[position - 1].column;
    match(TokenType::NEWLINE);
    return continue_stmt;
}

// ===== Import and Use Statements =====

// graphics.shapes -> "graphics.shapes" (any word is allowed, even keyword-lexed ones like `random`)
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

std::unique_ptr<UseStmt> PyCppParser::parseUseStmt() {
    auto use_stmt = std::make_unique<UseStmt>();
    use_stmt->line = tokens[position - 1].line;
    use_stmt->column = tokens[position - 1].column;

    // Expect a LIBRARY_NAME token for the entire <library> part
    if (check(TokenType::LIBRARY_NAME)) {
        std::string library = advance().value;

        // Remove the angle brackets to get just the library name
        if (library.length() >= 2 && library[0] == '<' && library[library.length() - 1] == '>') {
            use_stmt->library = library.substr(1, library.length() - 2);
        }
        else {
            use_stmt->library = library; // Fallback
        }

        pycpp::debugLog() << "Parsed use statement for library: " << use_stmt->library << std::endl;
    }
    else {
        // Fallback: try to parse < library > format
        consume(TokenType::LESS, "Expected '<' or library name after 'use'");

        if (check(TokenType::IDENTIFIER)) {
            use_stmt->library = advance().value;
        }
        else {
            throw std::runtime_error("Expected library name after '<'");
        }

        consume(TokenType::GREATER, "Expected '>' after library name");
    }

    // Consume newline
    match(TokenType::NEWLINE);

    return use_stmt;
}

// Parse an indented block. The caller has already consumed the ':' and the NEWLINE
// that follows it. A block starts at an INDENT token and ends at the matching
// DEDENT token produced by the lexer, so nested blocks close at exactly the
// indentation level they were written at.
std::vector<std::unique_ptr<Stmt>> PyCppParser::parseBlock() {
    std::vector<std::unique_ptr<Stmt>> statements;
    pycpp::debugLog() << "Starting to parse block at position " << position << std::endl;

    handleIndentation();

    if (!check(TokenType::INDENT)) {
        throw std::runtime_error("Expected an indented block at line " +
            std::to_string(peek().line) + ":" + std::to_string(peek().column));
    }
    advance(); // consume INDENT

    while (!atEnd()) {
        handleIndentation(); // skip blank lines

        if (check(TokenType::DEDENT)) {
            advance(); // consume the DEDENT that closes this block
            pycpp::debugLog() << "End of block (dedent)" << std::endl;
            break;
        }

        if (atEnd()) {
            break;
        }

        if (check(TokenType::INDENT)) {
            // A line indented deeper than the block it belongs to
            reportError("unexpected indentation", peek().line, peek().column);
            skipIndentedRegion();
            continue;
        }

        size_t before = position;
        try {
            if (auto stmt = parseStatement()) {
                statements.push_back(std::move(stmt));
                pycpp::debugLog() << "Added statement to block, now has " << statements.size() << " statements" << std::endl;
            }
            else {
                pycpp::debugLog() << "No statement parsed at position " << position << ", breaking from block" << std::endl;
                break;
            }
        }
        catch (const std::runtime_error& e) {
            reportError(e.what(), peek().line, peek().column);
            synchronizeInBlock();
        }

        if (position == before) {
            // No progress: skip the offending token so we cannot loop forever
            advance();
        }
    }

    pycpp::debugLog() << "Finished parsing block with " << statements.size() << " statements" << std::endl;
    return statements;
}

// Skip an INDENT ... DEDENT region (including any regions nested inside it).
void PyCppParser::skipIndentedRegion() {
    if (!check(TokenType::INDENT)) return;
    int depth = 0;
    while (!atEnd()) {
        if (check(TokenType::INDENT)) {
            depth++;
        }
        else if (check(TokenType::DEDENT)) {
            depth--;
            if (depth == 0) {
                advance();
                return;
            }
        }
        advance();
    }
}

// Error recovery inside a block: skip to the end of the current line without
// consuming INDENT/DEDENT tokens, so the block structure stays balanced. If the
// failed statement opened a nested block, that whole block is skipped too.
void PyCppParser::synchronizeInBlock() {
    while (!atEnd() && !check(TokenType::NEWLINE) &&
        !check(TokenType::INDENT) && !check(TokenType::DEDENT)) {
        advance();
    }
    match(TokenType::NEWLINE);
    handleIndentation();
    if (check(TokenType::INDENT)) {
        skipIndentedRegion();
    }
}

// ===== Expression Parsing =====

// ===== Source spans =====

// Span from token `first` up to the last consumed token.
void PyCppParser::finishSpan(Expr* e, size_t first) {
    if (!e || first >= tokens.size() || position == 0) return;
    const Token& a = tokens[first];
    const Token& b = tokens[std::min(position, tokens.size()) - 1];
    e->span_line = a.line;
    e->span_column = a.column;
    e->span_end_line = b.end_line;
    e->span_end_column = b.end_column;
}

// Span covering `from` up to the end of `to`.
static void joinSpan(Expr* e, const Expr* from, const Expr* to) {
    if (!e || !from || !to) return;
    e->span_line = from->span_line > 0 ? from->span_line : from->line;
    e->span_column = from->span_line > 0 ? from->span_column : from->column;
    e->span_end_line = to->span_end_line > 0 ? to->span_end_line : to->line;
    e->span_end_column = to->span_end_line > 0 ? to->span_end_column : to->column + 1;
}

// left <op> right. The node's line/column is the operator; its span is the whole expression.
std::unique_ptr<Expr> PyCppParser::makeBinary(std::unique_ptr<Expr> left, const Token& op_token,
                                              const std::string& op, std::unique_ptr<Expr> right) {
    auto node = std::make_unique<BinaryOp>();
    node->line = op_token.line;
    node->column = op_token.column;
    joinSpan(node.get(), left.get(), right.get());
    node->left = std::move(left);
    node->op = op;
    node->right = std::move(right);
    return node;
}

// ===== Expression Parsing =====

std::unique_ptr<Expr> PyCppParser::parseExpression() {
    return parseAssignment();
}

std::unique_ptr<Expr> PyCppParser::parseAssignment() {
    // Handle walrus operator first
    if (check(TokenType::IDENTIFIER) && checkNext(TokenType::COLON) &&
        position + 2 < tokens.size() && tokens[position + 2].type == TokenType::EQUAL) {
        return parseWalrusOperator();
    }

    // lambda has the lowest precedence of all expressions
    if (check(TokenType::LAMBDA)) {
        return parseLambda();
    }

    auto expr = parseBooleanOperations();

    // Conditional expression:  value_if_true if condition else value_if_false
    if (check(TokenType::IF)) {
        Token if_token = advance();
        auto ternary = std::make_unique<TernaryOp>();
        ternary->then_expr = std::move(expr);
        ternary->condition = parseBooleanOperations();
        consume(TokenType::ELSE, "Expected 'else' in conditional expression");
        ternary->else_expr = parseAssignment();
        ternary->line = if_token.line;
        ternary->column = if_token.column;
        joinSpan(ternary.get(), ternary->then_expr.get(), ternary->else_expr.get());
        return ternary;
    }

    if (match(TokenType::EQUAL)) {
        const Token op_token = tokens[position - 1];
        auto value = parseAssignment();
        return createAssignment(std::move(expr), op_token, "=", std::move(value));
    }

    static const std::pair<TokenType, const char*> compound[] = {
        {TokenType::PLUS_EQUAL, "+="}, {TokenType::MINUS_EQUAL, "-="}, {TokenType::STAR_EQUAL, "*="},
        {TokenType::SLASH_EQUAL, "/="}, {TokenType::DOUBLE_STAR_EQUAL, "**="},
        {TokenType::FLOOR_DIVIDE_EQUAL, "//="}, {TokenType::MODULO_EQUAL, "%="},
        {TokenType::BITWISE_AND_EQUAL, "&="}, {TokenType::BITWISE_OR_EQUAL, "|="},
        {TokenType::BITWISE_XOR_EQUAL, "^="}, {TokenType::LEFT_SHIFT_EQUAL, "<<="},
        {TokenType::RIGHT_SHIFT_EQUAL, ">>="},
    };
    for (const auto& entry : compound) {
        if (match(entry.first)) {
            const Token op_token = tokens[position - 1];
            auto value = parseAssignment();
            return createCompoundAssignment(std::move(expr), op_token, entry.second, std::move(value));
        }
    }

    return expr;
}

std::unique_ptr<Expr> PyCppParser::createAssignment(std::unique_ptr<Expr> left, const Token& op_token,
                                                    const std::string& op, std::unique_ptr<Expr> right) {
    if (!isValidAssignmentTarget(left.get())) {
        throw std::runtime_error("Invalid assignment target");
    }
    return makeBinary(std::move(left), op_token, op, std::move(right));
}

std::unique_ptr<Expr> PyCppParser::createCompoundAssignment(std::unique_ptr<Expr> left, const Token& op_token,
                                                            const std::string& op, std::unique_ptr<Expr> right) {
    if (!isValidAssignmentTarget(left.get())) {
        throw std::runtime_error("Invalid assignment target for compound assignment");
    }
    return makeBinary(std::move(left), op_token, op, std::move(right));   // "+=", "-=", ...
}

// lambda a, b: a + b
std::unique_ptr<Expr> PyCppParser::parseLambda() {
    const size_t first = position;
    Token keyword = advance();   // 'lambda'
    auto lambda = std::make_unique<Lambda>();
    lambda->line = keyword.line;
    lambda->column = keyword.column;
    if (!check(TokenType::COLON)) {
        do {
            if (check(TokenType::STAR) || check(TokenType::DOUBLE_STAR))
                throw std::runtime_error("*args and **kwargs in a lambda are not supported yet by PyCPP");
            lambda->params.push_back(consumeName("Expected a parameter name in lambda"));
            if (check(TokenType::EQUAL))
                throw std::runtime_error("default values for lambda parameters are not supported yet by PyCPP");
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::COLON, "Expected ':' after the lambda parameters");
    lambda->body = parseAssignment();
    finishSpan(lambda.get(), first);
    return lambda;
}

// Python precedence: `not` binds looser than comparisons, tighter than and/or.
std::unique_ptr<Expr> PyCppParser::parseNot() {
    if (check(TokenType::NOT)) {
        const size_t first = position;
        Token t = advance();
        auto u = std::make_unique<UnaryOp>();
        u->op = "not";
        u->line = t.line;
        u->column = t.column;
        u->operand = parseNot();
        finishSpan(u.get(), first);
        return u;
    }
    return parseComparisonWithIs();
}

std::unique_ptr<Expr> PyCppParser::parseBooleanOperations() {
    auto expr = parseNot();
    while (check(TokenType::AND) || check(TokenType::OR)) {
        const Token op = advance();
        expr = makeBinary(std::move(expr), op, op.type == TokenType::AND ? "and" : "or", parseNot());
    }
    return expr;
}

std::unique_ptr<Expr> PyCppParser::parseComparisonWithIs() {
    auto expr = parseComparison();

    while (true) {
        if (check(TokenType::IS)) {
            const Token op = advance();
            const bool negated = match(TokenType::NOT);   // is not
            expr = makeBinary(std::move(expr), op, negated ? "is not" : "is", parseComparison());
        }
        else if (check(TokenType::IN)) {
            const Token op = advance();
            expr = makeBinary(std::move(expr), op, "in", parseComparison());
        }
        else if (check(TokenType::NOT) && checkNext(TokenType::IN)) {
            const Token op = advance();
            advance();                                    // not in
            expr = makeBinary(std::move(expr), op, "not in", parseComparison());
        }
        else {
            break;
        }
    }
    return expr;
}

std::unique_ptr<Expr> PyCppParser::parseComparison() {
    auto expr = parseBitOr();

    auto isComparison = [&] {
        return check(TokenType::LESS) || check(TokenType::GREATER) || check(TokenType::LESS_EQUAL) ||
               check(TokenType::GREATER_EQUAL) || check(TokenType::DOUBLE_EQUAL) || check(TokenType::NOT_EQUAL);
    };
    if (isComparison()) {
        const Token op = advance();
        expr = makeBinary(std::move(expr), op, op.value, parseBitOr());
        if (isComparison())   // [fix2]
            throw std::runtime_error("chained comparisons such as a < b < c are not supported");
    }
    return expr;
}

// [fix2] bitwise operators, loosest to tightest:  |  ^  &  << >>   (all bind tighter than comparisons)
std::unique_ptr<Expr> PyCppParser::parseBitOr() {
    auto expr = parseBitXor();
    while (check(TokenType::BITWISE_OR)) {
        const Token op = advance();
        expr = makeBinary(std::move(expr), op, "|", parseBitXor());
    }
    return expr;
}

std::unique_ptr<Expr> PyCppParser::parseBitXor() {
    auto expr = parseBitAnd();
    while (check(TokenType::BITWISE_XOR)) {
        const Token op = advance();
        expr = makeBinary(std::move(expr), op, "^", parseBitAnd());
    }
    return expr;
}

std::unique_ptr<Expr> PyCppParser::parseBitAnd() {
    auto expr = parseShift();
    while (check(TokenType::BITWISE_AND)) {
        const Token op = advance();
        expr = makeBinary(std::move(expr), op, "&", parseShift());
    }
    return expr;
}

std::unique_ptr<Expr> PyCppParser::parseShift() {
    auto expr = parseTerm();
    while (check(TokenType::LEFT_SHIFT) || check(TokenType::RIGHT_SHIFT)) {
        const Token op = advance();
        expr = makeBinary(std::move(expr), op, op.value, parseTerm());
    }
    return expr;
}

std::unique_ptr<Expr> PyCppParser::parseTerm() {
    auto expr = parseFactor();
    while (check(TokenType::PLUS) || check(TokenType::MINUS)) {
        const Token op = advance();
        expr = makeBinary(std::move(expr), op, op.value, parseFactor());
    }
    return expr;
}

std::unique_ptr<Expr> PyCppParser::parseCall(std::unique_ptr<Expr> callee) {
    auto call = std::make_unique<CallExpr>();
    call->line = tokens[position - 1].line;
    call->column = tokens[position - 1].column;
    const int span_line = callee->span_line > 0 ? callee->span_line : callee->line;
    const int span_column = callee->span_line > 0 ? callee->span_column : callee->column;
    call->callee = std::move(callee);

    pycpp::debugLog() << "Parsing function call at position " << position << std::endl;

    // Arguments
    consume(TokenType::LPAREN, "Expected '(' after function name");

    if (!check(TokenType::RPAREN)) {
        do {
            if (check(TokenType::RPAREN)) break;   // trailing comma
            call->arguments.push_back(parseExpression());
            // A generator expression as the only argument: sum(x * x for x in xs)
            if (check(TokenType::FOR) && call->arguments.size() == 1) {
                auto comp = std::make_unique<Comprehension>();
                comp->line = call->arguments[0]->line;
                comp->column = call->arguments[0]->column;
                comp->element = std::move(call->arguments[0]);
                parseComprehensionClauses(*comp);
                joinSpan(comp.get(), comp->element.get(), comp->element.get());
                comp->span_end_line = tokens[position - 1].end_line;
                comp->span_end_column = tokens[position - 1].end_column;
                call->arguments[0] = std::move(comp);
                break;
            }
        } while (match(TokenType::COMMA));
    }

    consume(TokenType::RPAREN, "Expected ')' after arguments");
    call->span_line = span_line;
    call->span_column = span_column;
    call->span_end_line = tokens[position - 1].end_line;
    call->span_end_column = tokens[position - 1].end_column;

    return call;
}

// `for a, b in xs if cond` clauses after the element of a comprehension
void PyCppParser::parseComprehensionClauses(Comprehension& comp) {
    while (check(TokenType::FOR)) {
        CompFor clause;
        const Token keyword = advance();
        clause.line = keyword.line;
        clause.column = keyword.column;
        do {
            clause.var_names.push_back(consumeName("Expected a loop variable name in the comprehension"));
        } while (match(TokenType::COMMA));
        consume(TokenType::IN, "Expected 'in' after the comprehension variable");
        clause.iterable = parseBooleanOperations();   // no conditional expression here, as in Python
        while (match(TokenType::IF)) {
            clause.conditions.push_back(parseBooleanOperations());
        }
        comp.clauses.push_back(std::move(clause));
    }
}

// Prefix operators: -x, +x, &x (address-of), *p (dereference)
std::unique_ptr<Expr> PyCppParser::parseUnary() {
    if (check(TokenType::MINUS) || check(TokenType::PLUS) ||
        check(TokenType::BITWISE_AND) || check(TokenType::STAR)) {
        const size_t first = position;
        Token op_token = advance();

        // -9223372036854775808 is the smallest i64; its magnitude alone does not fit in 64 bits
        if (op_token.type == TokenType::MINUS && check(TokenType::NUMBER) &&
            peek().value == "9223372036854775808") {
            advance();
            auto lit = std::make_unique<IntLiteral>();
            lit->value = std::numeric_limits<long long>::min();
            lit->line = op_token.line;
            lit->column = op_token.column;
            finishSpan(lit.get(), first);
            return lit;
        }

        auto unary = std::make_unique<UnaryOp>();
        unary->op = op_token.value;
        unary->line = op_token.line;
        unary->column = op_token.column;
        unary->operand = parseUnary();
        finishSpan(unary.get(), first);
        return unary;
    }
    return parsePrimary();
}

std::unique_ptr<Expr> PyCppParser::parseFactor() {
    auto expr = parseUnary();
    while (check(TokenType::STAR) || check(TokenType::SLASH) || check(TokenType::PERCENT) ||
           check(TokenType::DOUBLE_STAR) || check(TokenType::FLOOR_DIVIDE)) {
        const Token op = advance();
        expr = makeBinary(std::move(expr), op, op.value, parseUnary());
    }
    return expr;
}

std::unique_ptr<Expr> PyCppParser::parsePrimary() {
    if (atEnd()) {
        throw std::runtime_error("Unexpected end of file while parsing expression");
    }

    pycpp::debugLog() << "parsePrimary at position " << position << ", token: '" << peek().value << "'" << std::endl;

    // Add recursion depth check
    static thread_local int recursion_depth = 0;
    if (recursion_depth > 100) {
        throw std::runtime_error("Expression parsing recursion depth exceeded");
    }
    recursion_depth++;

    std::unique_ptr<Expr> result = nullptr;
    const size_t first = position;   // first token of this primary, for its source span

    try {
        // Handle built-in function identifiers FIRST
        if (isBuiltInFunctionToken(peek().type)) {
            std::string func_name = peek().value;
            Token func_token = advance(); // consume the function name

            auto ident = std::make_unique<Identifier>();
            ident->name = func_name;
            ident->line = func_token.line;
            ident->column = func_token.column;

            // Check if this is a function call
            if (check(TokenType::LPAREN)) {
                result = parseCall(std::move(ident));
            }
            else {
                result = std::move(ident);
            }
        }
        else if (match(TokenType::NUMBER)) {
            // Check if it's a float (contains decimal point)
            if (tokens[position - 1].value.find_first_of(".eE") != std::string::npos) {
                auto lit = std::make_unique<FloatLiteral>();
                lit->value = std::stod(tokens[position - 1].value);
                lit->line = tokens[position - 1].line;
                lit->column = tokens[position - 1].column;
                result = std::move(lit);
            }
            else {
                auto lit = std::make_unique<IntLiteral>();
                try {
                    lit->value = std::stoll(tokens[position - 1].value);
                }
                catch (const std::exception&) {
                    throw std::runtime_error("integer literal '" + tokens[position - 1].value +
                                             "' is out of range: the largest supported integer is 9223372036854775807");
                }
                lit->line = tokens[position - 1].line;
                lit->column = tokens[position - 1].column;
                result = std::move(lit);
            }
        }
        else if (check(TokenType::FSTRING)) {
            result = parseFString(advance());
        }
        else if (match(TokenType::STRING_LITERAL)) {
            auto lit = std::make_unique<StringLiteral>();
            lit->value = tokens[position - 1].value;
            lit->line = tokens[position - 1].line;
            lit->column = tokens[position - 1].column;
            result = std::move(lit);
        }
        // Handle boolean literals
        else if (check(TokenType::TRUE) || check(TokenType::FALSE)) {
            auto lit = std::make_unique<BoolLiteral>();
            lit->value = (advance().type == TokenType::TRUE);
            lit->line = tokens[position - 1].line;
            lit->column = tokens[position - 1].column;
            result = std::move(lit);
        }
        // Handle None literal
        else if (match(TokenType::NONE)) {
            auto lit = std::make_unique<NoneLiteral>();
            lit->line = tokens[position - 1].line;
            lit->column = tokens[position - 1].column;
            result = std::move(lit);
        }
        else if (match(TokenType::IDENTIFIER)) {
            std::unique_ptr<Expr> current_expr = std::make_unique<Identifier>();
            auto& ident = *static_cast<Identifier*>(current_expr.get());
            ident.name = tokens[position - 1].value;
            ident.line = tokens[position - 1].line;
            ident.column = tokens[position - 1].column;

            // C++ scope qualification (std::sqrt, std::string): the lexer yields two
            // adjacent ':' tokens, which are folded into the identifier's name.
            while (position + 2 < tokens.size() &&
                   tokens[position].type == TokenType::COLON &&
                   tokens[position + 1].type == TokenType::COLON &&
                   tokens[position + 1].line == tokens[position].line &&
                   tokens[position + 1].column == tokens[position].column + 1 &&
                   isWordToken(tokens[position + 2])) {
                // After '::' any word is a C++ name, even one that PyCPP lexes as a
                // keyword (std::sqrt, std::min, std::string, ...).
                ident.name += "::" + tokens[position + 2].value;
                position += 3;
            }

            // Handle member access (obj.attr) - UPDATED to use MemberAccess
            while (match(TokenType::DOT)) {
                if (!atEnd() && isWordToken(peek())) {
                    std::string member_name = advance().value;
                    int member_line = tokens[position - 1].line;
                    int member_column = tokens[position - 1].column;

                    auto member_access = std::make_unique<MemberAccess>(
                        std::move(current_expr),
                        member_name,
                        member_line,
                        member_column
                    );
                    finishSpan(member_access.get(), first);
                    current_expr = std::move(member_access);
                }
                else {
                    throw std::runtime_error("Expected identifier after '.'");
                }
            }

            // Explicit type arguments before a call: mem.alloc[Player](...), Container[int](42)
            bool takes_type_args = false;
            if (check(TokenType::LBRACKET)) {
                if (auto member = dynamic_cast<MemberAccess*>(current_expr.get())) {
                    auto base = dynamic_cast<Identifier*>(member->object.get());
                    takes_type_args = base && base->name == "mem";
                }
                else if (auto name = dynamic_cast<Identifier*>(current_expr.get())) {
                    takes_type_args = generic_names.count(name->name) > 0;
                }
            }

            if (takes_type_args) {
                auto type_args = parseTypeArgs();
                auto call = parseCall(std::move(current_expr));
                static_cast<CallExpr*>(call.get())->type_args = std::move(type_args);
                result = std::move(call);
            }
            // === FIX: Check if it's a function call OR class constructor ===
            else if (check(TokenType::LPAREN)) {
                // Store the identifier name for constructor detection
                std::string callee_name = ident.name;

                // Parse as regular call first
                result = parseCall(std::move(current_expr));

                // No special handling needed - in C++, ClassName() is valid for constructors
                // The generated C++ code will handle this correctly
                pycpp::debugLog() << "Parsed call expression, might be constructor: " << callee_name << std::endl;
            }
            else if (check(TokenType::LBRACKET)) {
                // Handle array subscript
                advance(); // consume '['
                auto index = parseExpression();
                consume(TokenType::RBRACKET, "Expected ']' after index");

                // Create a call expression for array access
                auto array_access = std::make_unique<CallExpr>();
                array_access->callee = std::move(current_expr);
                array_access->is_subscript = true;
                array_access->arguments.push_back(std::move(index));
                array_access->line = tokens[position - 1].line;
                array_access->column = tokens[position - 1].column;
                finishSpan(array_access.get(), first);
                result = std::move(array_access);
            }
            else {
                finishSpan(current_expr.get(), first);
                result = std::move(current_expr);
            }
        }
        else if (match(TokenType::LBRACKET)) {
            result = parseListLiteral();
        }
        else if (match(TokenType::LBRACE)) {
            result = parseDictLiteral();
        }
        else if (match(TokenType::LPAREN)) {
            auto expr = parseExpression();
            consume(TokenType::RPAREN, "Expected ')' after expression");
            result = std::move(expr);
        }
        // Allow explicit casts: int(x), float(x), str(x), bool(x)
        else if (check(TokenType::INT) || check(TokenType::FLOAT) ||
                 check(TokenType::STR) || check(TokenType::BOOL) ||
                 check(TokenType::LIST) || check(TokenType::DICT) ||
                 check(TokenType::SET) || check(TokenType::TUPLE)) {
            std::string type_name = peek().value;
            advance();
            
            if (check(TokenType::LPAREN)) {
                auto ident = std::make_unique<Identifier>();
                ident->name = type_name;
                ident->line = tokens[position - 1].line;
                ident->column = tokens[position - 1].column;
                result = parseCall(std::move(ident));
            }
            else {
                throw std::runtime_error("Expected '(' after cast type '" + type_name + "'");
            }
        }
        else {
            // If we get here and the token is a colon or equals, it might be part of a declaration
            if (check(TokenType::COLON) || check(TokenType::EQUAL)) {
                result = nullptr; // Let the caller handle this as a declaration
            }
            else {
                if (peek().type == TokenType::UNKNOWN && peek().value.size() > 1)
                    throw std::runtime_error(peek().value);   // message produced by the lexer
                if (peek().type == TokenType::UNKNOWN)
                    throw std::runtime_error("unexpected character '" + peek().value + "'");
                if (peek().type == TokenType::NEWLINE || peek().type == TokenType::DEDENT)
                    throw std::runtime_error("expected an expression, but the line ended");
                switch (peek().type) {   // [fix2] Python keywords PyCPP does not implement
                case TokenType::WITH:
                    throw std::runtime_error(withHint("'with' statements are not supported yet by PyCPP",
                                             "use `defer` to run cleanup code when the enclosing block ends, e.g. `defer f.close()`"));
                case TokenType::YIELD:
                    throw std::runtime_error(withHint("generators ('yield') are not supported yet by PyCPP",
                                             "build and return a list instead, for example with a list comprehension"));
                case TokenType::ASYNC: case TokenType::AWAIT:
                    throw std::runtime_error(withHint("'" + peek().value + "' is not supported yet by PyCPP",
                                             "for concurrency, use C++ threads through `use <thread>`"));
                case TokenType::GLOBAL: case TokenType::NONLOCAL:
                    throw std::runtime_error(withHint("'" + peek().value + "' is not supported yet by PyCPP",
                                             "pass the value as a parameter and return the new value, or keep it in an object"));
                case TokenType::EXEC: case TokenType::EVAL: case TokenType::COMPILE:
                    throw std::runtime_error(withHint("'" + peek().value + "' is not supported by PyCPP",
                                             "PyCPP is compiled ahead of time, so code cannot be evaluated at run time"));
                default: break;
                }
                throw std::runtime_error("Expected expression, got: '" + peek().value + "'");
            }
        }

        if (result && result->span_line == 0) finishSpan(result.get(), first);
        else if (result && first < tokens.size() && tokens[first].type == TokenType::LPAREN)
            finishSpan(result.get(), first);   // (a + b): include the parentheses

        // [fix.py] Postfix operators after ANY primary expression, not just after a bare
        // identifier: (*p).x   a[i].x   f(x).y   obj.method().field   a[i][j]
        while (result) {
            if (check(TokenType::DOT)) {
                advance();
                if (atEnd() || !isWordToken(peek())) {
                    throw std::runtime_error("Expected identifier after '.'");
                }
                Token member = advance();
                result = std::make_unique<MemberAccess>(std::move(result), member.value,
                                                        member.line, member.column);
                finishSpan(result.get(), first);
            }
            else if (check(TokenType::LBRACKET)) {
                Token open = advance();
                auto index = parseExpression();
                consume(TokenType::RBRACKET, "Expected ']' after index");
                auto subscript = std::make_unique<CallExpr>();
                subscript->callee = std::move(result);
                subscript->is_subscript = true;
                subscript->arguments.push_back(std::move(index));
                subscript->line = open.line;
                subscript->column = open.column;
                result = std::move(subscript);
                finishSpan(result.get(), first);
            }
            else if (check(TokenType::LPAREN)) {
                result = parseCall(std::move(result));
                finishSpan(result.get(), first);
            }
            else {
                break;
            }
        }
    }
    catch (...) {
        recursion_depth--;
        throw;
    }

    recursion_depth--;

    if (result) {
        pycpp::debugLog() << "parsePrimary successfully parsed expression at position " << position << std::endl;
    }
    else {
        pycpp::debugLog() << "parsePrimary returned nullptr at position " << position << std::endl;
    }

    return result;
}
std::unique_ptr<Expr> PyCppParser::parseListLiteral() {
    auto list = std::make_unique<ListLiteral>();
    list->line = tokens[position - 1].line;
    list->column = tokens[position - 1].column;

    pycpp::debugLog() << "Parsing list literal at position " << position << std::endl;

    if (!check(TokenType::RBRACKET)) {
        list->elements.push_back(parseExpression());
        if (check(TokenType::FOR)) {   // [x * 2 for x in xs if x > 0]
            auto comp = std::make_unique<Comprehension>();
            comp->kind = Comprehension::Kind::List;
            comp->line = list->line;
            comp->column = list->column;
            comp->element = std::move(list->elements[0]);
            parseComprehensionClauses(*comp);
            consume(TokenType::RBRACKET, "Expected ']' after the list comprehension");
            return comp;
        }
        while (match(TokenType::COMMA)) {
            if (check(TokenType::RBRACKET)) break;   // trailing comma
            list->elements.push_back(parseExpression());
        }
    }

    consume(TokenType::RBRACKET, "Expected ']' after list elements");
    return list;
}

std::unique_ptr<Expr> PyCppParser::parseDictLiteral() {
    auto dict = std::make_unique<DictLiteral>();
    dict->line = tokens[position - 1].line;
    dict->column = tokens[position - 1].column;

    pycpp::debugLog() << "Parsing dict literal at position " << position << std::endl;

    if (!check(TokenType::RBRACE)) {
        auto first_key = parseExpression();
        if (check(TokenType::FOR)) {   // {x for x in xs}: set comprehension
            auto comp = std::make_unique<Comprehension>();
            comp->kind = Comprehension::Kind::Set;
            comp->line = dict->line;
            comp->column = dict->column;
            comp->element = std::move(first_key);
            parseComprehensionClauses(*comp);
            consume(TokenType::RBRACE, "Expected '}' after the set comprehension");
            return comp;
        }
        consume(TokenType::COLON, "Expected ':' after dictionary key");
        auto first_value = parseExpression();
        if (check(TokenType::FOR)) {   // {k: v for k, v in pairs}: dict comprehension
            auto comp = std::make_unique<Comprehension>();
            comp->kind = Comprehension::Kind::Dict;
            comp->line = dict->line;
            comp->column = dict->column;
            comp->element = std::move(first_key);
            comp->value = std::move(first_value);
            parseComprehensionClauses(*comp);
            consume(TokenType::RBRACE, "Expected '}' after the dict comprehension");
            return comp;
        }
        dict->items.emplace_back(std::move(first_key), std::move(first_value));
        while (match(TokenType::COMMA)) {
            if (check(TokenType::RBRACE)) break;   // trailing comma
            auto key = parseExpression();
            consume(TokenType::COLON, "Expected ':' after dictionary key");
            auto value = parseExpression();
            dict->items.emplace_back(std::move(key), std::move(value));
        }
    }

    consume(TokenType::RBRACE, "Expected '}' after dictionary items");
    return dict;
}

std::unique_ptr<Expr> PyCppParser::parseWalrusOperator() {
    // Save position for error reporting
    int line = peek().line;
    int column = peek().column;

    // Parse the identifier
    std::string var_name = consumeName("Expected variable name");
    consume(TokenType::COLON, "Expected ':' in walrus operator");
    consume(TokenType::EQUAL, "Expected '=' in walrus operator");

    // Parse the expression
    auto value = parseExpression();

    // Create both a variable declaration and the expression
    auto var_decl = std::make_unique<VarDecl>();
    var_decl->var_name = var_name;
    var_decl->type = std::make_unique<BasicType>();
    var_decl->type->name = "auto"; // Type inference
    var_decl->value = std::move(value);
    var_decl->line = line;
    var_decl->column = column;

    // For now, we'll create a special node that represents both declaration and expression
    // In a full implementation, you might want a dedicated WalrusOperator node
    auto ident = std::make_unique<Identifier>();
    ident->name = var_name;
    ident->line = line;
    ident->column = column;

    // Note: This is a simplification. In a real implementation, you'd need to
    // handle the declaration separately from the expression.
    return ident;
}

std::unique_ptr<Expr> PyCppParser::parseTernaryOperator() {
    auto condition = parseExpression();
    consume(TokenType::IF, "Expected 'if' in ternary operator");
    auto then_expr = parseExpression();
    consume(TokenType::ELSE, "Expected 'else' in ternary operator");
    auto else_expr = parseExpression();

    auto ternary = std::make_unique<TernaryOp>();
    ternary->condition = std::move(condition);
    ternary->then_expr = std::move(then_expr);
    ternary->else_expr = std::move(else_expr);
    ternary->line = tokens[position - 1].line;
    ternary->column = tokens[position - 1].column;

    return ternary;
}

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

// try:        <block>
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

// defer stmt      |      defer:  <indented block>
std::unique_ptr<Stmt> PyCppParser::parseDeferStmt() {
    auto d = std::make_unique<DeferStmt>();
    d->line = tokens[position - 1].line;
    d->column = tokens[position - 1].column;
    if (match(TokenType::COLON)) {
        consume(TokenType::NEWLINE, "Expected newline after 'defer:'");
        d->body = parseBlock();
    }
    else {
        auto s = parseStatement();
        if (!s) throw std::runtime_error("Expected a statement after 'defer'");
        d->body.push_back(std::move(s));
    }
    return d;
}

// match subject:
//     case 1 | 2:   ...
//     case _:       ...
std::unique_ptr<Stmt> PyCppParser::parseMatchStmt() {
    auto m = std::make_unique<MatchStmt>();
    m->line = tokens[position - 1].line;
    m->column = tokens[position - 1].column;
    m->subject = parseExpression();
    consume(TokenType::COLON, "Expected ':' after match subject");
    consume(TokenType::NEWLINE, "Expected newline after match subject");
    handleIndentation();
    if (!check(TokenType::INDENT)) throw std::runtime_error("Expected indented 'case' blocks after 'match'");
    advance();

    while (!atEnd()) {
        handleIndentation();
        if (check(TokenType::DEDENT)) { advance(); break; }
        if (atEnd()) break;

        MatchCase c;
        c.line = peek().line;
        c.column = peek().column;
        consume(TokenType::CASE, "Expected 'case' inside a match block");
        do {   // case 1 | 2 | 3:
            auto pattern = parseUnary();
            auto id = dynamic_cast<Identifier*>(pattern.get());
            if (id && id->name == "_") c.is_default = true;
            else c.patterns.push_back(std::move(pattern));
        } while (match(TokenType::BITWISE_OR));
        consume(TokenType::COLON, "Expected ':' after case pattern");
        if (check(TokenType::NEWLINE)) {
            advance();
            c.body = parseBlock();
        }
        else if (auto s = parseStatement()) {
            c.body.push_back(std::move(s));
        }
        m->cases.push_back(std::move(c));
    }
    return m;
}

// ===== f-strings =====

// Escape sequences in the literal text of an f-string (the lexer keeps them raw).
static std::string unescapeFStringText(const std::string& raw) {
    std::string out;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\' || i + 1 >= raw.size()) { out += raw[i]; continue; }
        const char c = raw[++i];
        switch (c) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case '\\': out += '\\'; break;
        case '"': out += '"'; break;
        case '\'': out += '\''; break;
        default: out += '\\'; out += c; break;
        }
    }
    return out;
}

// f"a {x + 1:>5} b {y!r} {z=}"  ->  FString with literal and replacement-field parts.
// Each field's expression is lexed and parsed on its own, with token columns shifted
// so that diagnostics point into the f-string.
std::unique_ptr<Expr> PyCppParser::parseFString(const Token& token) {
    auto fs = std::make_unique<FString>();
    fs->line = token.line;
    fs->column = token.column;
    const std::string& raw = token.value;
    const int body_column = token.column + 2;   // after f and the opening quote

    std::string text;
    auto flushText = [&] {
        if (text.empty()) return;
        FStringPart part;
        part.text = unescapeFStringText(text);
        fs->parts.push_back(std::move(part));
        text.clear();
    };
    auto fail = [&](const std::string& message, size_t offset) {
        reportError(message, token.line, body_column + static_cast<int>(offset));
    };

    for (size_t i = 0; i < raw.size(); ++i) {
        const char c = raw[i];
        if (c == '\\' && i + 1 < raw.size()) { text += c; text += raw[++i]; continue; }
        if (c == '{' && i + 1 < raw.size() && raw[i + 1] == '{') { text += '{'; ++i; continue; }
        if (c == '}' && i + 1 < raw.size() && raw[i + 1] == '}') { text += '}'; ++i; continue; }
        if (c == '}') { fail("f-string: single '}' is not allowed; write '}}' for a literal brace", i); continue; }
        if (c != '{') { text += c; continue; }

        // Find the end of the replacement field, skipping nested brackets and strings
        const size_t open = i;
        size_t j = i + 1;
        int depth = 0;
        char quote = 0;
        size_t colon = std::string::npos, bang = std::string::npos;
        for (; j < raw.size(); ++j) {
            const char d = raw[j];
            if (quote) { if (d == quote) quote = 0; continue; }
            if (d == '"' || d == '\'') { quote = d; continue; }
            if (d == '(' || d == '[' || d == '{') { depth++; continue; }
            if ((d == ')' || d == ']' || d == '}') && depth > 0) { depth--; continue; }
            if (d == '}' && depth == 0) break;
            if (depth == 0 && d == '!' && bang == std::string::npos && colon == std::string::npos &&
                j + 1 < raw.size() && raw[j + 1] != '=') bang = j;
            if (depth == 0 && d == ':' && colon == std::string::npos) colon = j;
        }
        if (j >= raw.size()) { fail("f-string: '{' is never closed; write '{{' for a literal brace", open); break; }

        flushText();
        FStringPart part;
        size_t expr_end = std::min({ j, colon, bang });
        if (colon != std::string::npos && colon < j) part.spec = raw.substr(colon + 1, j - colon - 1);
        if (bang != std::string::npos && bang < j) {
            const size_t conv_end = colon != std::string::npos && colon < j ? colon : j;
            const std::string conv = raw.substr(bang + 1, conv_end - bang - 1);
            if (conv != "r" && conv != "s") {
                fail("f-string: unknown conversion '!" + conv + "'; use !r or !s", bang);
                i = j;
                continue;
            }
            part.conversion = conv[0];
        }
        std::string expr_text = raw.substr(open + 1, expr_end - open - 1);
        // f"{x=}" prints "x=" before the value
        {
            const size_t last = expr_text.find_last_not_of(" \t");
            if (last != std::string::npos && expr_text[last] == '=' &&
                (last == 0 || std::string("=!<>").find(expr_text[last - 1]) == std::string::npos)) {
                part.debug_text = expr_text.substr(0, last + 1);
                expr_text = expr_text.substr(0, last);
                if (!part.conversion && part.spec.empty()) part.conversion = 'r';
            }
        }
        if (expr_text.find_first_not_of(" \t") == std::string::npos) {
            fail("f-string: empty expression in '{}'", open);
            i = j;
            continue;
        }

        // Lex and parse the expression on its own, then move its tokens to where they are
        Lexer lexer(expr_text);
        std::vector<Token> field_tokens = lexer.tokenize();
        const int shift = body_column + static_cast<int>(open + 1) - 1;
        std::vector<Token> placed;
        for (auto t : field_tokens) {
            if (t.type == TokenType::NEWLINE || t.type == TokenType::INDENT || t.type == TokenType::DEDENT) continue;
            t.line = token.line;
            t.column += shift;
            t.end_line = token.line;
            t.end_column += shift;
            placed.push_back(t);
        }
        PyCppParser sub(placed);
        sub.generic_names = generic_names;
        try {
            part.expr = sub.parseExpression();
            if (!sub.atEnd())
                throw std::runtime_error("f-string: unexpected '" + sub.peek().value + "' in the replacement field");
        }
        catch (const std::runtime_error& e) {
            const Token where = sub.atEnd() ? placed.empty() ? token : placed.back() : sub.peek();
            reportError(e.what(), where.line, where.column);
            part.expr.reset();
        }
        for (auto& err : sub.errors) errors.push_back(err);
        if (part.expr) fs->parts.push_back(std::move(part));
        i = j;
    }
    flushText();
    return fs;
}
