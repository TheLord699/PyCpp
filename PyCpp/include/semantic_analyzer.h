#ifndef SEMANTIC_ANALYZER_H
#define SEMANTIC_ANALYZER_H

#include "../include/ast.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <string>

class SemanticAnalyzer : public ASTVisitor {
private:
    struct Symbol {
        std::string name;
        std::string type;
        bool is_initialized;
        int scope_level;
    };

    struct FunctionSymbol {
        std::string name;
        std::string return_type;
        bool has_return;
        std::vector<std::pair<std::string, std::string>> parameters;
    };

    struct ClassSymbol {
        std::string name;
        std::string base_class;
        std::unordered_map<std::string, Symbol> attributes;
        std::unordered_map<std::string, FunctionSymbol> methods;
        bool is_struct = false;  // ADD THIS LINE - distinguishes structs from classes
    };

    struct AttributeSymbol {
        std::string name;
        std::string type;
        bool is_initialized;
        int scope_level;
    };

    // Symbol tables
    std::vector<std::unordered_map<std::string, Symbol>> scopes;
    std::unordered_map<std::string, FunctionSymbol> functions;
    std::unordered_map<std::string, ClassSymbol> classes;
    std::unordered_set<std::string> generic_params; // type parameters such as T from def f[T] / class C[T]

    // Current context
    int current_scope;
    FunctionSymbol* current_function;
    ClassSymbol* current_class;
    int unsafe_depth = 0; // > 0 while inside an 'unsafe:' block

    // Built-in usage tracking
    std::unordered_set<std::string> used_builtins;

    // Scope management
    void enterScope();
    void exitScope();
    void addSymbol(const std::string& name, const std::string& type, bool initialized = false);
    Symbol* findSymbol(const std::string& name);
    bool symbolExistsInCurrentScope(const std::string& name);

    // Type checking
    bool isValidType(const std::string& type);
    bool typesAreCompatible(const std::string& type1, const std::string& type2);
    std::string typeToString(Type* type);
    std::string getExpressionType(Expr* expr);
    bool isValidAssignmentTarget(Expr* expr);

    // Analysis methods
    void analyzeProgram(Program* program);
    void analyzeStatement(Stmt* stmt);
    void analyzeVarDecl(VarDecl* var);
    void analyzeFunction(FunctionDef* func);
    void analyzeClass(ClassDef* cls);
    void analyzeStruct(StructDef* struc);
    void analyzeExpression(Expr* expr);
    void analyzeIfStmt(IfStmt* if_stmt);
    void analyzeWhileStmt(WhileStmt* while_stmt);
    void analyzeForStmt(ForStmt* for_stmt);
    void analyzeReturnStmt(ReturnStmt* return_stmt);

    // NEW: Import statement analysis
    void analyzeImportStmt(ImportStmt* import);

    // NEW: Extract member variables from constructors
    void extractMembersFromConstructor(FunctionDef* constructor, ClassDef* cls);

    // Built-in usage tracking
    void trackBuiltinUsage(const std::string& builtin);

    // Error reporting
    void error(const std::string& message, int line, int column);
    void warning(const std::string& message, int line, int column);

public:
    SemanticAnalyzer();
    void analyze(std::unique_ptr<Program>& program);

    // Visitor pattern implementations
    void visit(Program& node) override;
    void visit(FunctionDef& node) override;
    void visit(ClassDef& node) override;
    void visit(StructDef& node) override;
    void visit(TemplateDef& node) override;
    void visit(ConstexprDef& node) override;
    void visit(VarDecl& node) override;
    void visit(ReturnStmt& node) override;
    void visit(PassStmt& node) override;
    void visit(BreakStmt& node) override;
    void visit(ContinueStmt& node) override;
    void visit(IfStmt& node) override;
    void visit(WhileStmt& node) override;
    void visit(ForStmt& node) override;
    void visit(UnsafeBlock& node) override;
    void visit(ExprStmt& node) override;
    void visit(Block& node) override;
    void visit(IntLiteral& node) override;
    void visit(FloatLiteral& node) override;
    void visit(StringLiteral& node) override;
    void visit(BoolLiteral& node) override;
    void visit(NoneLiteral& node) override;
    void visit(Identifier& node) override;
    void visit(BinaryOp& node) override;
    void visit(UnaryOp& node) override;
    void visit(TernaryOp& node) override;
    void visit(CallExpr& node) override;
    void visit(ListLiteral& node) override;
    void visit(DictLiteral& node) override;
    void visit(MemberAccess& node) override;
    void visit(BasicType& node) override;
    void visit(PointerType& node) override;
    void visit(ListType& node) override;
    void visit(DictType& node) override;
    void visit(ImportStmt& node) override;
    void visit(UseStmt& node) override;
};

#endif // SEMANTIC_ANALYZER_H