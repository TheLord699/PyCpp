#ifndef LEXER_H
#define LEXER_H

#include <string>
#include <vector>

enum class TokenType {
    // Identifiers and literals
    IDENTIFIER, NUMBER, STRING_LITERAL,

    // Keywords
    DEF, IF, ELSE, ELIF, WHILE, FOR, RETURN, PASS, BREAK, CONTINUE,
    YIELD, ASYNC, AWAIT, WITH,
    CLASS, STRUCT, IMPORT, FROM, AS, IN, USE, TEMPLATE, TYPENAME,
    CONSTEXPR, UNSAFE, LAMBDA,
    DEFER, MATCH, CASE,
    AND, OR, NOT, IS,
    TRUE, FALSE, NONE,

    // Types
    INT, INT32, INT64, FLOAT, FLOAT32, FLOAT64, DOUBLE, BOOL, STR, STRING,
    CHAR, VOID, AUTO, NONE_TYPE,
    LIST, DICT, TUPLE, SET, VECTOR, MAP, ARRAY,

    // Built-in functions
    PRINT, INPUT, LEN, RANGE, ENUMERATE, ZIP, MAP_FUNC, FILTER, REDUCE,
    SORTED, REVERSED, MIN, MAX, SUM, ABS, ROUND, POW, DIVMOD,
    BIN, OCT, HEX, CHR, ORD, TYPE_FUNC, ISINSTANCE, ISSUBCLASS,
    HASATTR, GETATTR, SETATTR, DELATTR, PROPERTY, STATICMETHOD, CLASSMETHOD,

    // Type conversion functions
    INT_FUNC, FLOAT_FUNC, STR_FUNC, BOOL_FUNC, LIST_FUNC, DICT_FUNC,
    TUPLE_FUNC, SET_FUNC,

    // String methods
    FORMAT, JOIN, SPLIT, STRIP, REPLACE, FIND, STARTSWITH, ENDSWITH,
    LOWER, UPPER, CAPITALIZE, TITLE,

    // File operations
    OPEN, READ, WRITE, CLOSE, SEEK, TELL,

    // Math functions
    MATH, SQRT, SIN, COS, TAN, LOG, LOG10, EXP, CEIL, FLOOR,

    // Random functions
    RANDOM, RANDINT, CHOICE, SHUFFLE, UNIFORM,

    // System functions
    EXIT, QUIT, HELP, DIR, VARS, LOCALS, GLOBALS, ID, HASH,

    // Memory management
    DEL, GC, MALLOC, FREE,

    // Exception handling
    TRY, EXCEPT, FINALLY, RAISE, ASSERT,

    // Advanced features
    GLOBAL, NONLOCAL, EXEC, EVAL, COMPILE,

    // Special methods
    INIT, STR_SPECIAL, REPR, LEN_SPECIAL, GETITEM, SETITEM, DELITEM,
    ITER, NEXT, CALL, ADD_SPECIAL, SUB_SPECIAL, MUL_SPECIAL, TRUEDIV_SPECIAL,
    FLOORDIV_SPECIAL, MOD_SPECIAL, POW_SPECIAL, EQ_SPECIAL, NE_SPECIAL,
    LT_SPECIAL, LE_SPECIAL, GT_SPECIAL, GE_SPECIAL,

    // Operators
    PLUS, MINUS, STAR, SLASH, PERCENT,
    PLUS_EQUAL, MINUS_EQUAL, STAR_EQUAL, SLASH_EQUAL, MODULO_EQUAL,
    DOUBLE_STAR, DOUBLE_STAR_EQUAL, FLOOR_DIVIDE, FLOOR_DIVIDE_EQUAL,
    EQUAL, DOUBLE_EQUAL, NOT_EQUAL,
    LESS, GREATER, LESS_EQUAL, GREATER_EQUAL,
    BITWISE_AND, BITWISE_OR, BITWISE_XOR,
    BITWISE_AND_EQUAL, BITWISE_OR_EQUAL, BITWISE_XOR_EQUAL,
    LEFT_SHIFT, RIGHT_SHIFT, LEFT_SHIFT_EQUAL, RIGHT_SHIFT_EQUAL,

    // Other tokens
    LPAREN, RPAREN, LBRACKET, RBRACKET, LBRACE, RBRACE,
    COMMA, COLON, SEMICOLON, DOT, ELLIPSIS, ARROW, WALRUS,
    NEWLINE, INDENT, DEDENT,
    END_OF_FILE, UNKNOWN,

    // Special
    LIBRARY_NAME,
    FSTRING        // f"...": value is the raw text between the quotes
};

struct Token {
    TokenType type;
    std::string value;
    int line;
    int column;
    // Position just after the token's last character (set by Lexer::tokenize), so a
    // diagnostic can underline exactly the source text an expression came from.
    int end_line = 0;
    int end_column = 0;

    Token(TokenType t, const std::string& v, int l, int c)
        : type(t), value(v), line(l), column(c), end_line(l), end_column(c + static_cast<int>(v.size())) {
    }
};

class Lexer {
private:
    std::string source;
    size_t position;
    int line;
    int column;

    char peek();
    char advance();
    void skipWhitespace();
    void skipComment();
    Token readNumber();
    Token readString();
    Token readFString();
    Token readIdentifier();
    Token readOperator();

public:
    Lexer(const std::string& source);
    std::vector<Token> tokenize();
};

#endif