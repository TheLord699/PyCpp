#include <cctype>
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

void PyCppParser::reportError(const std::string& message, int line, int column) {
    errors.push_back("line " + std::to_string(line) + ":" + std::to_string(column) + ": syntax error: " + message);
}

std::unique_ptr<Program> PyCppParser::parse() {
    auto program = parseProgram();
    if (!errors.empty()) {
        std::string all;
        for (size_t i = 0; i < errors.size(); ++i) {
            if (i > 0) all += "\n   ";
            all += errors[i];
        }
        throw std::runtime_error(all);
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
        if (auto ident = dynamic_cast<Identifier*>(call->callee.get())) {
            return true; // Assume it's array access
        }
    }

    return false;
}

std::unique_ptr<Expr> PyCppParser::createAssignment(std::unique_ptr<Expr> left, const std::string& op, std::unique_ptr<Expr> right) {
    // Check if left side is valid for assignment
    if (isValidAssignmentTarget(left.get())) {
        auto assign = std::make_unique<BinaryOp>();
        assign->left = std::move(left);
        assign->op = op;
        assign->right = std::move(right);
        assign->line = tokens[position - 1].line;
        assign->column = tokens[position - 1].column;
        return assign;
    }
    throw std::runtime_error("Invalid assignment target");
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
            synchronize();
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
    if (check(TokenType::IDENTIFIER) && checkNext(TokenType::COLON)) {
        return parseVarDecl();
    }

    // Look for member variable declaration: identifier . identifier : type
    if (isMemberAccessDeclaration()) {
        return parseVarDecl();
    }

    // Only if it's not a declaration, try to parse as expression
    if (auto expr = parseExpression()) {
        auto stmt = std::make_unique<ExprStmt>();
        stmt->expr = std::move(expr);
        stmt->line = tokens[position].line;
        stmt->column = tokens[position].column;

        // Try to consume newline, but don't require it
        match(TokenType::NEWLINE);
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
            std::string param_name = consume(TokenType::IDENTIFIER, "Expected parameter name").value;
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

    // Consume newline if present
    match(TokenType::NEWLINE);

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
        var_decl->var_name = consume(TokenType::IDENTIFIER, "Expected variable name").value;
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

    // Consume newline if present
    match(TokenType::NEWLINE);

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
        for_stmt->var_names.push_back(consume(TokenType::IDENTIFIER, "Expected loop variable name").value);
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

    // Consume newline if present
    match(TokenType::NEWLINE);

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

// from module import name1, name2
std::unique_ptr<ImportStmt> PyCppParser::parseFromImportStmt() {
    auto import_stmt = std::make_unique<ImportStmt>();
    import_stmt->line = tokens[position - 1].line;
    import_stmt->column = tokens[position - 1].column;
    import_stmt->is_from = true;

    import_stmt->module = consume(TokenType::IDENTIFIER, "Expected module name after 'from'").value;
    consume(TokenType::IMPORT, "Expected 'import' after module name");

    do {
        import_stmt->imports.push_back(consume(TokenType::IDENTIFIER, "Expected name after 'import'").value);
    } while (match(TokenType::COMMA));

    match(TokenType::NEWLINE);
    return import_stmt;
}

std::unique_ptr<ImportStmt> PyCppParser::parseImportStmt() {
    auto import_stmt = std::make_unique<ImportStmt>();
    import_stmt->line = tokens[position - 1].line;
    import_stmt->column = tokens[position - 1].column;

    // Check for "from ... import ..." syntax
    if (check(TokenType::IDENTIFIER) && peek().value == "from") {
        advance(); // consume "from"
        import_stmt->module = consume(TokenType::IDENTIFIER, "Expected module name after 'from'").value;
        consume(TokenType::IMPORT, "Expected 'import' after module name");

        // Parse imported names
        do {
            import_stmt->imports.push_back(consume(TokenType::IDENTIFIER, "Expected identifier after 'import'").value);
        } while (match(TokenType::COMMA));
    }
    else {
        // Simple "import ..." syntax
        import_stmt->module = consume(TokenType::IDENTIFIER, "Expected module name").value;

        // Check for import with specific items: import module.item
        if (match(TokenType::DOT)) {
            if (check(TokenType::IDENTIFIER)) {
                import_stmt->imports.push_back(advance().value);
            }
        }
    }

    // Consume newline
    match(TokenType::NEWLINE);

    return import_stmt;
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

std::unique_ptr<Expr> PyCppParser::parseExpression() {
    return parseAssignment();
}

std::unique_ptr<Expr> PyCppParser::parseAssignment() {
    // Handle walrus operator first
    if (check(TokenType::IDENTIFIER) && checkNext(TokenType::COLON) &&
        position + 2 < tokens.size() && tokens[position + 2].type == TokenType::EQUAL) {
        return parseWalrusOperator();
    }

    auto expr = parseBooleanOperations();

    // Handle all assignment operators
    if (match(TokenType::EQUAL)) {
        auto value = parseAssignment();
        return createAssignment(std::move(expr), "=", std::move(value));
    }
    else if (match(TokenType::PLUS_EQUAL)) {
        auto value = parseAssignment();
        return createCompoundAssignment(std::move(expr), "+=", std::move(value));
    }
    else if (match(TokenType::MINUS_EQUAL)) {
        auto value = parseAssignment();
        return createCompoundAssignment(std::move(expr), "-=", std::move(value));
    }
    else if (match(TokenType::STAR_EQUAL)) {
        auto value = parseAssignment();
        return createCompoundAssignment(std::move(expr), "*=", std::move(value));
    }
    else if (match(TokenType::SLASH_EQUAL)) {
        auto value = parseAssignment();
        return createCompoundAssignment(std::move(expr), "/=", std::move(value));
    }
    else if (match(TokenType::DOUBLE_STAR_EQUAL)) {
        auto value = parseAssignment();
        return createCompoundAssignment(std::move(expr), "**=", std::move(value));
    }
    else if (match(TokenType::FLOOR_DIVIDE_EQUAL)) {
        auto value = parseAssignment();
        return createCompoundAssignment(std::move(expr), "//=", std::move(value));
    }
    else if (match(TokenType::MODULO_EQUAL)) {
        auto value = parseAssignment();
        return createCompoundAssignment(std::move(expr), "%=", std::move(value));
    }
    else if (match(TokenType::BITWISE_AND_EQUAL)) {
        auto value = parseAssignment();
        return createCompoundAssignment(std::move(expr), "&=", std::move(value));
    }
    else if (match(TokenType::BITWISE_OR_EQUAL)) {
        auto value = parseAssignment();
        return createCompoundAssignment(std::move(expr), "|=", std::move(value));
    }
    else if (match(TokenType::BITWISE_XOR_EQUAL)) {
        auto value = parseAssignment();
        return createCompoundAssignment(std::move(expr), "^=", std::move(value));
    }
    else if (match(TokenType::LEFT_SHIFT_EQUAL)) {
        auto value = parseAssignment();
        return createCompoundAssignment(std::move(expr), "<<=", std::move(value));
    }
    else if (match(TokenType::RIGHT_SHIFT_EQUAL)) {
        auto value = parseAssignment();
        return createCompoundAssignment(std::move(expr), ">>=", std::move(value));
    }

    return expr;
}

std::unique_ptr<Expr> PyCppParser::createCompoundAssignment(std::unique_ptr<Expr> left, const std::string& op, std::unique_ptr<Expr> right) {
    if (!isValidAssignmentTarget(left.get())) {
        throw std::runtime_error("Invalid assignment target for compound assignment");
    }

    // Create a BinaryOp with the compound operator
    auto compound_op = std::make_unique<BinaryOp>();
    compound_op->left = std::move(left);
    compound_op->op = op; // This will be "+=", "-=", etc.
    compound_op->right = std::move(right);
    compound_op->line = tokens[position - 1].line;
    compound_op->column = tokens[position - 1].column;

    return compound_op;
}

std::unique_ptr<Expr> PyCppParser::parseBooleanOperations() {
    auto expr = parseComparisonWithIs();

    while (true) {
        if (match(TokenType::AND)) {
            auto op = std::make_unique<BinaryOp>();
            op->left = std::move(expr);
            op->op = "and";
            op->right = parseComparisonWithIs();
            op->line = tokens[position - 1].line;
            op->column = tokens[position - 1].column;
            expr = std::move(op);
        }
        else if (match(TokenType::OR)) {
            auto op = std::make_unique<BinaryOp>();
            op->left = std::move(expr);
            op->op = "or";
            op->right = parseComparisonWithIs();
            op->line = tokens[position - 1].line;
            op->column = tokens[position - 1].column;
            expr = std::move(op);
        }
        else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<Expr> PyCppParser::parseComparisonWithIs() {
    auto expr = parseComparison();

    while (true) {
        if (match(TokenType::IS)) {
            // Handle 'is not'
            if (match(TokenType::NOT)) {
                auto op = std::make_unique<BinaryOp>();
                op->left = std::move(expr);
                op->op = "is not";
                op->right = parseComparison();
                op->line = tokens[position - 2].line; // 'is' token
                op->column = tokens[position - 2].column;
                expr = std::move(op);
            }
            else {
                auto op = std::make_unique<BinaryOp>();
                op->left = std::move(expr);
                op->op = "is";
                op->right = parseComparison();
                op->line = tokens[position - 1].line;
                op->column = tokens[position - 1].column;
                expr = std::move(op);
            }
        }
        else if (match(TokenType::IN)) {
            auto op = std::make_unique<BinaryOp>();
            op->left = std::move(expr);
            op->op = "in";
            op->right = parseComparison();
            op->line = tokens[position - 1].line;
            op->column = tokens[position - 1].column;
            expr = std::move(op);
        }
        else if (match(TokenType::NOT)) {
            if (match(TokenType::IN)) {
                auto op = std::make_unique<BinaryOp>();
                op->left = std::move(expr);
                op->op = "not in";
                op->right = parseComparison();
                op->line = tokens[position - 2].line; // 'not' token
                op->column = tokens[position - 2].column;
                expr = std::move(op);
            }
            else {
                // Unary not operator
                auto unary = std::make_unique<UnaryOp>();
                unary->op = "not";
                unary->operand = parseComparison();
                unary->line = tokens[position - 1].line;
                unary->column = tokens[position - 1].column;
                expr = std::move(unary);
                break; // Not continues the expression
            }
        }
        else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<Expr> PyCppParser::parseComparison() {
    auto expr = parseTerm();

    while (true) {
        if (match(TokenType::LESS)) {
            auto op = std::make_unique<BinaryOp>();
            op->left = std::move(expr);
            op->op = "<";
            op->right = parseTerm();
            op->line = tokens[position - 1].line;
            op->column = tokens[position - 1].column;
            expr = std::move(op);
        }
        else if (match(TokenType::GREATER)) {
            auto op = std::make_unique<BinaryOp>();
            op->left = std::move(expr);
            op->op = ">";
            op->right = parseTerm();
            op->line = tokens[position - 1].line;
            op->column = tokens[position - 1].column;
            expr = std::move(op);
        }
        else if (match(TokenType::LESS_EQUAL)) {
            auto op = std::make_unique<BinaryOp>();
            op->left = std::move(expr);
            op->op = "<=";
            op->right = parseTerm();
            op->line = tokens[position - 1].line;
            op->column = tokens[position - 1].column;
            expr = std::move(op);
        }
        else if (match(TokenType::GREATER_EQUAL)) {
            auto op = std::make_unique<BinaryOp>();
            op->left = std::move(expr);
            op->op = ">=";
            op->right = parseTerm();
            op->line = tokens[position - 1].line;
            op->column = tokens[position - 1].column;
            expr = std::move(op);
        }
        else if (match(TokenType::DOUBLE_EQUAL)) {
            auto op = std::make_unique<BinaryOp>();
            op->left = std::move(expr);
            op->op = "==";
            op->right = parseTerm();
            op->line = tokens[position - 1].line;
            op->column = tokens[position - 1].column;
            expr = std::move(op);
        }
        else if (match(TokenType::NOT_EQUAL)) {
            auto op = std::make_unique<BinaryOp>();
            op->left = std::move(expr);
            op->op = "!=";
            op->right = parseTerm();
            op->line = tokens[position - 1].line;
            op->column = tokens[position - 1].column;
            expr = std::move(op);
        }
        else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<Expr> PyCppParser::parseTerm() {
    auto expr = parseFactor();

    while (true) {
        if (match(TokenType::PLUS)) {
            auto op = std::make_unique<BinaryOp>();
            op->left = std::move(expr);
            op->op = "+";
            op->right = parseFactor();
            op->line = tokens[position - 1].line;
            op->column = tokens[position - 1].column;
            expr = std::move(op);
        }
        else if (match(TokenType::MINUS)) {
            auto op = std::make_unique<BinaryOp>();
            op->left = std::move(expr);
            op->op = "-";
            op->right = parseFactor();
            op->line = tokens[position - 1].line;
            op->column = tokens[position - 1].column;
            expr = std::move(op);
        }
        else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<Expr> PyCppParser::parseCall(std::unique_ptr<Expr> callee) {
    auto call = std::make_unique<CallExpr>();
    call->callee = std::move(callee);
    call->line = tokens[position - 1].line;
    call->column = tokens[position - 1].column;

    pycpp::debugLog() << "Parsing function call at position " << position << std::endl;

    // Arguments
    consume(TokenType::LPAREN, "Expected '(' after function name");

    if (!check(TokenType::RPAREN)) {
        do {
            call->arguments.push_back(parseExpression());
        } while (match(TokenType::COMMA));
    }

    consume(TokenType::RPAREN, "Expected ')' after arguments");

    return call;
}

// Prefix operators: -x, +x, &x (address-of), *p (dereference)
std::unique_ptr<Expr> PyCppParser::parseUnary() {
    if (check(TokenType::MINUS) || check(TokenType::PLUS) ||
        check(TokenType::BITWISE_AND) || check(TokenType::STAR)) {
        Token op_token = advance();
        auto unary = std::make_unique<UnaryOp>();
        unary->op = op_token.value;
        unary->line = op_token.line;
        unary->column = op_token.column;
        unary->operand = parseUnary();
        return unary;
    }
    return parsePrimary();
}

std::unique_ptr<Expr> PyCppParser::parseFactor() {
    auto expr = parseUnary();

    while (true) {
        if (match(TokenType::STAR)) {
            auto op = std::make_unique<BinaryOp>();
            op->left = std::move(expr);
            op->op = "*";
            op->right = parseUnary();
            op->line = tokens[position - 1].line;
            op->column = tokens[position - 1].column;
            expr = std::move(op);
        }
        else if (match(TokenType::SLASH)) {
            auto op = std::make_unique<BinaryOp>();
            op->left = std::move(expr);
            op->op = "/";
            op->right = parseUnary();
            op->line = tokens[position - 1].line;
            op->column = tokens[position - 1].column;
            expr = std::move(op);
        }
        else if (match(TokenType::PERCENT)) {
            auto op = std::make_unique<BinaryOp>();
            op->left = std::move(expr);
            op->op = "%";
            op->right = parseUnary();
            op->line = tokens[position - 1].line;
            op->column = tokens[position - 1].column;
            expr = std::move(op);
        }
        else if (match(TokenType::DOUBLE_STAR)) {
            auto op = std::make_unique<BinaryOp>();
            op->left = std::move(expr);
            op->op = "**";
            op->right = parseUnary();
            op->line = tokens[position - 1].line;
            op->column = tokens[position - 1].column;
            expr = std::move(op);
        }
        else if (match(TokenType::FLOOR_DIVIDE)) {
            auto op = std::make_unique<BinaryOp>();
            op->left = std::move(expr);
            op->op = "//";
            op->right = parseUnary();
            op->line = tokens[position - 1].line;
            op->column = tokens[position - 1].column;
            expr = std::move(op);
        }
        else {
            break;
        }
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
            if (tokens[position - 1].value.find('.') != std::string::npos) {
                auto lit = std::make_unique<FloatLiteral>();
                lit->value = std::stod(tokens[position - 1].value);
                lit->line = tokens[position - 1].line;
                lit->column = tokens[position - 1].column;
                result = std::move(lit);
            }
            else {
                auto lit = std::make_unique<IntLiteral>();
                lit->value = std::stoi(tokens[position - 1].value);
                lit->line = tokens[position - 1].line;
                lit->column = tokens[position - 1].column;
                result = std::move(lit);
            }
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
                result = std::move(array_access);
            }
            else {
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
                 check(TokenType::STR) || check(TokenType::BOOL)) {
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
                throw std::runtime_error("Expected expression, got: '" + peek().value + "'");
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
        do {
            list->elements.push_back(parseExpression());
        } while (match(TokenType::COMMA));
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
        do {
            auto key = parseExpression();
            consume(TokenType::COLON, "Expected ':' after dictionary key");
            auto value = parseExpression();
            dict->items.emplace_back(std::move(key), std::move(value));
        } while (match(TokenType::COMMA));
    }

    consume(TokenType::RBRACE, "Expected '}' after dictionary items");
    return dict;
}

std::unique_ptr<Expr> PyCppParser::parseWalrusOperator() {
    // Save position for error reporting
    int line = peek().line;
    int column = peek().column;

    // Parse the identifier
    std::string var_name = consume(TokenType::IDENTIFIER, "Expected variable name").value;
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
