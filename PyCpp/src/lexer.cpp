#include "../include/lexer.h"
#include <cctype>
#include <unordered_map>

Lexer::Lexer(const std::string& source)
    : source(source), position(0), line(1), column(1) {
}

char Lexer::peek() {
    if (position >= source.length()) return '\0';
    return source[position];
}

char Lexer::advance() {
    if (position >= source.length()) return '\0';
    char c = source[position++];
    if (c == '\n') {
        line++;
        column = 1;
    }
    else {
        column++;
    }
    return c;
}

void Lexer::skipWhitespace() {
    while (std::isspace(peek()) && peek() != '\n') {
        advance();
    }
}

void Lexer::skipComment() {
    if (peek() == '#') {
        while (peek() != '\n' && peek() != '\0') {
            advance();
        }
    }
}

Token Lexer::readNumber() {
    std::string number;
    int start_line = line;
    int start_column = column;

    while (std::isdigit(peek())) {
        number += advance();
    }

    // Hexadecimal and binary literals are normalised to decimal text
    if (number == "0" && (peek() == 'x' || peek() == 'X')) {
        number += advance();
        while (std::isxdigit(static_cast<unsigned char>(peek()))) number += advance();
        if (number.size() <= 2) return Token(TokenType::UNKNOWN, number, start_line, start_column);
        return Token(TokenType::NUMBER, std::to_string(std::stoull(number, nullptr, 16)), start_line, start_column);
    }
    if (number == "0" && (peek() == 'b' || peek() == 'B') && position + 1 < source.length() &&
        (source[position + 1] == '0' || source[position + 1] == '1')) {
        advance();
        std::string bits;
        while (peek() == '0' || peek() == '1') bits += advance();
        return Token(TokenType::NUMBER, std::to_string(std::stoull(bits, nullptr, 2)), start_line, start_column);
    }

    // Check for decimal point
    if (peek() == '.') {
        number += advance();
        while (std::isdigit(peek())) {
            number += advance();
        }
    }

    // Exponent: 1e5, 2.5E-3
    if ((peek() == 'e' || peek() == 'E') && position + 1 < source.length()) {
        const char next = source[position + 1];
        const size_t digit_at = (next == '+' || next == '-') ? position + 2 : position + 1;
        if (digit_at < source.length() && std::isdigit(static_cast<unsigned char>(source[digit_at]))) {
            number += advance();
            if (next == '+' || next == '-') number += advance();
            while (std::isdigit(peek())) number += advance();
        }
    }

    return Token(TokenType::NUMBER, number, start_line, start_column);
}

Token Lexer::readString() {
    int start_line = line;
    int start_column = column;
    char quote = advance(); // Skip opening quote
    std::string str;

    while (peek() != quote && peek() != '\0' && peek() != '\n') {   // [diag-fix] strings end at the line end
        if (peek() == '\\') {
            advance(); // Skip backslash
            // Handle escape sequences
            switch (peek()) {
            case 'n': str += '\n'; break;
            case 't': str += '\t'; break;
            case 'r': str += '\r'; break;
            case '\\': str += '\\'; break;
            case '"': str += '"'; break;
            case '\'': str += '\''; break;
            default: str += '\\'; str += peek(); break;
            }
            advance();
        }
        else {
            str += advance();
        }
    }

    if (peek() == quote) {
        advance(); // Skip closing quote
    }
    else {
        return Token(TokenType::UNKNOWN, "unterminated string literal", start_line, start_column);
    }

    return Token(TokenType::STRING_LITERAL, str, start_line, start_column);
}

Token Lexer::readIdentifier() {
    std::string identifier;
    int start_line = line;
    int start_column = column;

    while (std::isalnum(peek()) || peek() == '_') {
        identifier += advance();
    }

    // Enhanced keyword mapping with extensive Pythonic features
    static std::unordered_map<std::string, TokenType> keywords = {
        // Control flow
        {"def", TokenType::DEF},
        {"if", TokenType::IF},
        {"else", TokenType::ELSE},
        {"elif", TokenType::ELIF},
        {"while", TokenType::WHILE},
        {"for", TokenType::FOR},
        {"return", TokenType::RETURN},
        {"pass", TokenType::PASS},
        {"break", TokenType::BREAK},
        {"continue", TokenType::CONTINUE},
        {"yield", TokenType::YIELD},
        {"async", TokenType::ASYNC},
        {"await", TokenType::AWAIT},
        {"with", TokenType::WITH},

        // Types and classes
        {"class", TokenType::CLASS},
        {"struct", TokenType::STRUCT},
        {"import", TokenType::IMPORT},
        {"from", TokenType::FROM},
        {"as", TokenType::AS},
        {"in", TokenType::IN},
        {"is", TokenType::IS},
        {"use", TokenType::USE},
        {"template", TokenType::TEMPLATE},
        {"typename", TokenType::TYPENAME},
        {"constexpr", TokenType::CONSTEXPR},
        {"unsafe", TokenType::UNSAFE},
        {"lambda", TokenType::LAMBDA},
        {"defer", TokenType::DEFER},
        {"match", TokenType::MATCH},
        {"case", TokenType::CASE},

        // Boolean operators
        {"and", TokenType::AND},
        {"or", TokenType::OR},
        {"not", TokenType::NOT},

        // Literals
        {"True", TokenType::TRUE},
        {"False", TokenType::FALSE},
        {"None", TokenType::NONE},

        // Type keywords
        {"int", TokenType::INT},
        {"i32", TokenType::INT32},
        {"i64", TokenType::INT64},
        {"float", TokenType::FLOAT},
        {"f32", TokenType::FLOAT32},
        {"f64", TokenType::FLOAT64},
        {"double", TokenType::DOUBLE},
        {"bool", TokenType::BOOL},
        {"str", TokenType::STR},
        {"string", TokenType::STRING},
        {"char", TokenType::CHAR},
        {"void", TokenType::VOID},
        {"auto", TokenType::AUTO},
        {"NoneType", TokenType::NONE_TYPE},

        // Collection types
        {"list", TokenType::LIST},
        {"dict", TokenType::DICT},
        {"tuple", TokenType::TUPLE},
        {"set", TokenType::SET},
        {"vector", TokenType::VECTOR},
        {"map", TokenType::MAP},
        {"array", TokenType::ARRAY},

        // Built-in functions
        {"print", TokenType::PRINT},
        {"input", TokenType::INPUT},
        {"len", TokenType::LEN},
        {"range", TokenType::RANGE},
        {"enumerate", TokenType::ENUMERATE},
        {"zip", TokenType::ZIP},
        {"map", TokenType::MAP_FUNC},
        {"filter", TokenType::FILTER},
        {"reduce", TokenType::REDUCE},
        {"sorted", TokenType::SORTED},
        {"reversed", TokenType::REVERSED},
        {"min", TokenType::MIN},
        {"max", TokenType::MAX},
        {"sum", TokenType::SUM},
        {"abs", TokenType::ABS},
        {"round", TokenType::ROUND},
        {"pow", TokenType::POW},
        {"divmod", TokenType::DIVMOD},
        {"bin", TokenType::BIN},
        {"oct", TokenType::OCT},
        {"hex", TokenType::HEX},
        {"chr", TokenType::CHR},
        {"ord", TokenType::ORD},
        {"type", TokenType::TYPE_FUNC},
        {"isinstance", TokenType::ISINSTANCE},
        {"issubclass", TokenType::ISSUBCLASS},
        {"hasattr", TokenType::HASATTR},
        {"getattr", TokenType::GETATTR},
        {"setattr", TokenType::SETATTR},
        {"delattr", TokenType::DELATTR},
        {"property", TokenType::PROPERTY},
        {"staticmethod", TokenType::STATICMETHOD},
        {"classmethod", TokenType::CLASSMETHOD},

        // Type conversion functions
        {"int", TokenType::INT_FUNC},
        {"float", TokenType::FLOAT_FUNC},
        {"str", TokenType::STR_FUNC},
        {"bool", TokenType::BOOL_FUNC},
        {"list", TokenType::LIST_FUNC},
        {"dict", TokenType::DICT_FUNC},
        {"tuple", TokenType::TUPLE_FUNC},
        {"set", TokenType::SET_FUNC},

        // String methods
        {"format", TokenType::FORMAT},
        {"join", TokenType::JOIN},
        {"split", TokenType::SPLIT},
        {"strip", TokenType::STRIP},
        {"replace", TokenType::REPLACE},
        {"find", TokenType::FIND},
        {"startswith", TokenType::STARTSWITH},
        {"endswith", TokenType::ENDSWITH},
        {"lower", TokenType::LOWER},
        {"upper", TokenType::UPPER},
        {"capitalize", TokenType::CAPITALIZE},
        {"title", TokenType::TITLE},

        // File operations
        {"open", TokenType::OPEN},
        {"read", TokenType::READ},
        {"write", TokenType::WRITE},
        {"close", TokenType::CLOSE},
        {"seek", TokenType::SEEK},
        {"tell", TokenType::TELL},

        // Math functions
        {"math", TokenType::MATH},
        {"sqrt", TokenType::SQRT},
        {"sin", TokenType::SIN},
        {"cos", TokenType::COS},
        {"tan", TokenType::TAN},
        {"log", TokenType::LOG},
        {"log10", TokenType::LOG10},
        {"exp", TokenType::EXP},
        {"ceil", TokenType::CEIL},
        {"floor", TokenType::FLOOR},

        // Random functions
        {"random", TokenType::RANDOM},
        {"randint", TokenType::RANDINT},
        {"choice", TokenType::CHOICE},
        {"shuffle", TokenType::SHUFFLE},
        {"uniform", TokenType::UNIFORM},

        // System functions
        {"exit", TokenType::EXIT},
        {"quit", TokenType::QUIT},
        {"help", TokenType::HELP},
        {"dir", TokenType::DIR},
        {"vars", TokenType::VARS},
        {"locals", TokenType::LOCALS},
        {"globals", TokenType::GLOBALS},
        {"id", TokenType::ID},
        {"hash", TokenType::HASH},

        // Memory management
        {"del", TokenType::DEL},
        {"gc", TokenType::GC},
        {"malloc", TokenType::MALLOC},
        {"free", TokenType::FREE},

        // Exception handling
        {"try", TokenType::TRY},
        {"except", TokenType::EXCEPT},
        {"finally", TokenType::FINALLY},
        {"raise", TokenType::RAISE},
        {"assert", TokenType::ASSERT},

        // Advanced features
        {"global", TokenType::GLOBAL},
        {"nonlocal", TokenType::NONLOCAL},
        {"exec", TokenType::EXEC},
        {"eval", TokenType::EVAL},
        {"compile", TokenType::COMPILE},

        // Special methods
        {"__init__", TokenType::INIT},
        {"__str__", TokenType::STR_SPECIAL},
        {"__repr__", TokenType::REPR},
        {"__len__", TokenType::LEN_SPECIAL},
        {"__getitem__", TokenType::GETITEM},
        {"__setitem__", TokenType::SETITEM},
        {"__delitem__", TokenType::DELITEM},
        {"__iter__", TokenType::ITER},
        {"__next__", TokenType::NEXT},
        {"__call__", TokenType::CALL},
        {"__add__", TokenType::ADD_SPECIAL},
        {"__sub__", TokenType::SUB_SPECIAL},
        {"__mul__", TokenType::MUL_SPECIAL},
        {"__truediv__", TokenType::TRUEDIV_SPECIAL},
        {"__floordiv__", TokenType::FLOORDIV_SPECIAL},
        {"__mod__", TokenType::MOD_SPECIAL},
        {"__pow__", TokenType::POW_SPECIAL},
        {"__eq__", TokenType::EQ_SPECIAL},
        {"__ne__", TokenType::NE_SPECIAL},
        {"__lt__", TokenType::LT_SPECIAL},
        {"__le__", TokenType::LE_SPECIAL},
        {"__gt__", TokenType::GT_SPECIAL},
        {"__ge__", TokenType::GE_SPECIAL}
    };

    auto it = keywords.find(identifier);
    if (it != keywords.end()) {
        return Token(it->second, identifier, start_line, start_column);
    }

    return Token(TokenType::IDENTIFIER, identifier, start_line, start_column);
}

Token Lexer::readOperator() {
    int start_line = line;
    int start_column = column;
    char c = advance();

    switch (c) {
    case '+':
        if (peek() == '=') {
            advance();
            return Token(TokenType::PLUS_EQUAL, "+=", start_line, start_column);
        }
        // (no '++' token: in PyCPP, a++b is a + (+b))
        return Token(TokenType::PLUS, "+", start_line, start_column);

    case '-':
        if (peek() == '=') {
            advance();
            return Token(TokenType::MINUS_EQUAL, "-=", start_line, start_column);
        }
        if (peek() == '>') {
            advance();
            return Token(TokenType::ARROW, "->", start_line, start_column);
        }
        // (no '--' token: in PyCPP, a--b is a - (-b))
        return Token(TokenType::MINUS, "-", start_line, start_column);

    case '*':
        if (peek() == '*') {
            advance();
            if (peek() == '=') {
                advance();
                return Token(TokenType::DOUBLE_STAR_EQUAL, "**=", start_line, start_column);
            }
            return Token(TokenType::DOUBLE_STAR, "**", start_line, start_column);
        }
        if (peek() == '=') {
            advance();
            return Token(TokenType::STAR_EQUAL, "*=", start_line, start_column);
        }
        return Token(TokenType::STAR, "*", start_line, start_column);

    case '/':
        if (peek() == '/') {
            advance();
            if (peek() == '=') {
                advance();
                return Token(TokenType::FLOOR_DIVIDE_EQUAL, "//=", start_line, start_column);
            }
            return Token(TokenType::FLOOR_DIVIDE, "//", start_line, start_column);
        }
        if (peek() == '=') {
            advance();
            return Token(TokenType::SLASH_EQUAL, "/=", start_line, start_column);
        }
        return Token(TokenType::SLASH, "/", start_line, start_column);

    case '%':
        if (peek() == '=') {
            advance();
            return Token(TokenType::MODULO_EQUAL, "%=", start_line, start_column);
        }
        return Token(TokenType::PERCENT, "%", start_line, start_column);

    case '&':
        if (peek() == '=') {
            advance();
            return Token(TokenType::BITWISE_AND_EQUAL, "&=", start_line, start_column);
        }
        return Token(TokenType::BITWISE_AND, "&", start_line, start_column);

    case '|':
        if (peek() == '=') {
            advance();
            return Token(TokenType::BITWISE_OR_EQUAL, "|=", start_line, start_column);
        }
        return Token(TokenType::BITWISE_OR, "|", start_line, start_column);

    case '^':
        if (peek() == '=') {
            advance();
            return Token(TokenType::BITWISE_XOR_EQUAL, "^=", start_line, start_column);
        }
        return Token(TokenType::BITWISE_XOR, "^", start_line, start_column);

    case '<':
        if (peek() == '<') {
            advance();
            if (peek() == '=') {
                advance();
                return Token(TokenType::LEFT_SHIFT_EQUAL, "<<=", start_line, start_column);
            }
            return Token(TokenType::LEFT_SHIFT, "<<", start_line, start_column);
        }
        if (peek() == '=') {
            advance();
            return Token(TokenType::LESS_EQUAL, "<=", start_line, start_column);
        }

        // For use statements, read the entire library name as one token
        if (std::isalpha(peek())) {
            std::string library;
            library += c; // Include the '<'

            // Read the library name
            while (std::isalnum(peek()) || peek() == '_' || peek() == '/' || peek() == '.') {
                library += advance();
            }

            // Include the closing '>'
            if (peek() == '>') {
                library += advance();
                return Token(TokenType::LIBRARY_NAME, library, start_line, start_column);
            }
            else {
                // If no closing '>', return as LESS and let parser handle the error
                return Token(TokenType::LESS, "<", start_line, start_column);
            }
        }
        return Token(TokenType::LESS, "<", start_line, start_column);

    case '>':
        if (peek() == '>') {
            advance();
            if (peek() == '=') {
                advance();
                return Token(TokenType::RIGHT_SHIFT_EQUAL, ">>=", start_line, start_column);
            }
            return Token(TokenType::RIGHT_SHIFT, ">>", start_line, start_column);
        }
        if (peek() == '=') {
            advance();
            return Token(TokenType::GREATER_EQUAL, ">=", start_line, start_column);
        }
        return Token(TokenType::GREATER, ">", start_line, start_column);

    case '=':
        if (peek() == '=') {
            advance();
            return Token(TokenType::DOUBLE_EQUAL, "==", start_line, start_column);
        }
        return Token(TokenType::EQUAL, "=", start_line, start_column);

    case '!':
        if (peek() == '=') {
            advance();
            return Token(TokenType::NOT_EQUAL, "!=", start_line, start_column);
        }
        return Token(TokenType::NOT, "!", start_line, start_column);

    case ':':
        if (peek() == '=') {
            advance();
            return Token(TokenType::WALRUS, ":=", start_line, start_column);
        }
        return Token(TokenType::COLON, ":", start_line, start_column);

    case '.':
        if (peek() == '.' && position + 1 < source.length() && source[position + 1] == '.') {
            advance(); // second dot
            advance(); // third dot
            return Token(TokenType::ELLIPSIS, "...", start_line, start_column);
        }
        return Token(TokenType::DOT, ".", start_line, start_column);

    case '(': return Token(TokenType::LPAREN, "(", start_line, start_column);
    case ')': return Token(TokenType::RPAREN, ")", start_line, start_column);
    case '[': return Token(TokenType::LBRACKET, "[", start_line, start_column);
    case ']': return Token(TokenType::RBRACKET, "]", start_line, start_column);
    case '{': return Token(TokenType::LBRACE, "{", start_line, start_column);
    case '}': return Token(TokenType::RBRACE, "}", start_line, start_column);
    case ',': return Token(TokenType::COMMA, ",", start_line, start_column);
    case ';': return Token(TokenType::SEMICOLON, ";", start_line, start_column);

    default: return Token(TokenType::UNKNOWN, std::string(1, c), start_line, start_column);
    }
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    std::vector<int> indent_stack = { 0 }; // Track indentation levels
    bool at_line_start = true;
    int current_indent = 0;
    int bracket_depth = 0; // Inside (), [] or {} indentation is not significant

    while (position < source.length()) {
        // Handle line starts and indentation
        if (at_line_start) {
            // Measure the indentation BEFORE any whitespace is skipped. (Calling
            // skipWhitespace() first consumed the leading spaces, so every line
            // measured as indent 0 and no INDENT/DEDENT token was ever produced.)
            current_indent = 0;
            while (position < source.length() &&
                (source[position] == ' ' || source[position] == '\t' || source[position] == '\r')) {
                if (source[position] == ' ') {
                    current_indent++;
                }
                else if (source[position] == '\t') {
                    current_indent += 4; // Treat tabs as 4 spaces
                }
                // '\r' (CRLF files) is consumed but does not count as indentation
                advance();
            }

            // Blank line: emit a NEWLINE and stay at line start
            // (inside brackets lines are joined, so no NEWLINE is produced)
            if (peek() == '\n') {
                if (bracket_depth == 0) {
                    tokens.emplace_back(TokenType::NEWLINE, "\n", line, column);
                }
                advance();
                continue;
            }

            // Comment-only line: indentation is irrelevant, the trailing newline
            // is emitted as a blank line on the next iteration
            if (peek() == '#') {
                skipComment();
                continue;
            }

            // Reached end of input after whitespace only
            if (peek() == '\0') {
                break;
            }

            // Inside brackets a continuation line must not open or close a block
            if (bracket_depth > 0) {
                at_line_start = false;
                continue;
            }

            // Handle indentation changes
            int previous_indent = indent_stack.back();

            if (current_indent > previous_indent) {
                // Increased indentation
                indent_stack.push_back(current_indent);
                tokens.emplace_back(TokenType::INDENT, "", line, column);
            }
            else if (current_indent < previous_indent) {
                // Decreased indentation - generate DEDENT tokens
                while (indent_stack.back() > current_indent) {
                    indent_stack.pop_back();
                    tokens.emplace_back(TokenType::DEDENT, "", line, column);
                }

                if (indent_stack.back() != current_indent) {
                    // Indentation error - mismatched levels
                    tokens.emplace_back(TokenType::UNKNOWN, "inconsistent indentation: this line does not line up with any enclosing block", line, column);
                }
            }

            at_line_start = false;
        }

        // Skip whitespace (non-newline) within line
        skipWhitespace();

        // Skip comments
        if (peek() == '#') {
            skipComment();
            // After comment, we're at line start again
            at_line_start = true;
            continue;
        }

        char c = peek();
        if (c == '\0') break;

        if (std::isdigit(c)) {
            tokens.push_back(readNumber());
        }
        else if (std::isalpha(c) || c == '_') {
            tokens.push_back(readIdentifier());
        }
        else if (c == '"' || c == '\'') {
            tokens.push_back(readString());
        }
        else if (std::ispunct(c)) {
            tokens.push_back(readOperator());
            switch (tokens.back().type) {
            case TokenType::LPAREN:
            case TokenType::LBRACKET:
            case TokenType::LBRACE:
                bracket_depth++;
                break;
            case TokenType::RPAREN:
            case TokenType::RBRACKET:
            case TokenType::RBRACE:
                if (bracket_depth > 0) bracket_depth--;
                break;
            default:
                break;
            }
        }
        else if (c == '\n') {
            // A newline inside (), [] or {} continues the expression (implicit
            // line joining), so it is not a statement terminator.
            if (bracket_depth == 0) {
                tokens.emplace_back(TokenType::NEWLINE, "\n", line, column);
            }
            advance();
            at_line_start = true; // Next token will be at line start
        }
        else {
            tokens.emplace_back(TokenType::UNKNOWN, std::string(1, c), line, column);
            advance();
        }
    }

    // Generate DEDENT tokens for any remaining indentation levels
    while (indent_stack.size() > 1) {
        indent_stack.pop_back();
        tokens.emplace_back(TokenType::DEDENT, "", line, column);
    }

    tokens.emplace_back(TokenType::END_OF_FILE, "", line, column);
    return tokens;
}