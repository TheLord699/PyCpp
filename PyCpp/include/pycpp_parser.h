#ifndef PYCPP_PARSER_H
#define PYCPP_PARSER_H

#include "log.h"
#include "../include/ast.h"
#include "../include/lexer.h"
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <set>

class PyCppParser {
private:
    std::vector<Token> tokens;
    std::vector<std::string> errors; // collected syntax errors; parse() throws if any
    void reportError(const std::string& message, int line, int column);
    size_t position;
    int current_indent;
    std::set<std::string> generic_names; // functions/classes declared with [T] parameters

    // Helper methods
    Token peek();
    Token advance();
    bool match(TokenType type);
    bool check(TokenType type);
    bool checkNext(TokenType type);
    Token consume(TokenType type, const std::string& error_msg);
    bool atEnd();
    void synchronize();
    void synchronizeInBlock();
    void skipIndentedRegion();
    void handleIndentation();
    bool isBuiltInFunctionToken(TokenType type);

    // Expression parsing helpers
    bool isMemberAccess();
    bool isMemberAccessDeclaration();
    bool isClassAttributeDeclaration();
    bool isValidAssignmentTarget(Expr* expr);
    std::unique_ptr<Expr> createAssignment(std::unique_ptr<Expr> left, const std::string& op, std::unique_ptr<Expr> right);
    std::unique_ptr<Expr> createCompoundAssignment(std::unique_ptr<Expr> left, const std::string& op, std::unique_ptr<Expr> right);

public:
    PyCppParser(const std::vector<Token>& tokens);
    std::unique_ptr<Program> parse();

    // Top-level parsing
    std::unique_ptr<Program> parseProgram();
    std::unique_ptr<Stmt> parseTopLevelStatement();
    std::unique_ptr<Stmt> parseStatement();

    // Declaration parsing
    std::unique_ptr<FunctionDef> parseFunctionDef();
    std::unique_ptr<ClassDef> parseClassDef();
    std::unique_ptr<StructDef> parseStructDef();
    std::unique_ptr<TemplateDef> parseTemplateDef();
    std::unique_ptr<ConstexprDef> parseConstexpr();
    std::unique_ptr<UnsafeBlock> parseUnsafeBlock();
    std::unique_ptr<VarDecl> parseVarDecl();
    std::unique_ptr<VarDecl> parseClassAttribute();

    // Type parsing
    std::unique_ptr<Type> parseType();

    // Control flow parsing
    std::unique_ptr<IfStmt> parseIfStmt();
    std::unique_ptr<WhileStmt> parseWhileStmt();
    std::unique_ptr<ForStmt> parseForStmt();
    std::unique_ptr<ReturnStmt> parseReturnStmt();
    std::unique_ptr<PassStmt> parsePassStmt();
    std::unique_ptr<BreakStmt> parseBreakStmt();
    std::unique_ptr<ContinueStmt> parseContinueStmt();
    std::unique_ptr<Stmt> parseAssertStmt();
    std::unique_ptr<Stmt> parseDeferStmt();
    std::unique_ptr<Stmt> parseTryStmt();
    std::unique_ptr<Stmt> parseRaiseStmt();
    std::unique_ptr<Stmt> parseMatchStmt();

    // Import and use statements
    std::unique_ptr<ImportStmt> parseImportStmt();
    std::unique_ptr<UseStmt> parseUseStmt();
    std::unique_ptr<ImportStmt> parseFromImportStmt();
    std::vector<std::string> parseTypeParams();
    std::vector<std::unique_ptr<Type>> parseTypeArgs();

    // Block parsing
    std::vector<std::unique_ptr<Stmt>> parseBlock();

    // Expression parsing
    std::unique_ptr<Expr> parseExpression();
    std::unique_ptr<Expr> parseAssignment();
    std::unique_ptr<Expr> parseBooleanOperations();
    std::unique_ptr<Expr> parseNot();
    std::unique_ptr<Expr> parseComparisonWithIs();
    std::unique_ptr<Expr> parseComparison();
    std::unique_ptr<Expr> parseTerm();
    std::unique_ptr<Expr> parseFactor();
    std::unique_ptr<Expr> parseUnary();
    std::unique_ptr<Expr> parsePrimary();
    std::unique_ptr<Expr> parseCall(std::unique_ptr<Expr> callee);
    std::unique_ptr<Expr> parseListLiteral();
    std::unique_ptr<Expr> parseDictLiteral();
    std::unique_ptr<Expr> parseWalrusOperator();
    std::unique_ptr<Expr> parseTernaryOperator();
};

#endif