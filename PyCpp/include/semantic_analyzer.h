#ifndef SEMANTIC_ANALYZER_H
#define SEMANTIC_ANALYZER_H

#include "../include/ast.h"
#include "diagnostics.h"
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
        int line = 0, column = 0;   // [fix2] where it was declared
        bool used = false;          // mentioned after its declaration
        bool is_var = false;        // a local variable (candidate for the unused warning)
    };

    struct FunctionSymbol {
        std::string name;
        std::string return_type;
        bool has_return;
        std::vector<std::pair<std::string, std::string>> parameters;
        size_t required = 0;      // [fix2] parameters without a default value
        bool generic = false;
        int line = 0;             // where it is defined (for notes)
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
    std::string module_stem;                        // name of this module's generated files (cross-module checks)
    std::unordered_map<std::string, int> ended_names;   // variables whose block has ended (name -> line)
    std::unordered_map<const Expr*, std::string> expr_types;   // types computed while a scope was open
    bool has_cpp_headers = false;  // a `use <header>` is present: C++ names are not ours to check

    // Current context
    int current_scope;
    FunctionSymbol* current_function;
    ClassSymbol* current_class;

    // Built-in usage tracking
    std::unordered_set<std::string> used_builtins;

    static std::unordered_map<std::string, ClassSymbol>& exportedClasses();
    static std::unordered_map<std::string, FunctionSymbol>& exportedFunctions();
    void registerClassMembers(ClassDef* cls);

    // Scope management
    void enterScope();
    void exitScope();
    void addSymbol(const std::string& name, const std::string& type, bool initialized = false, int line = 0, int column = 0);
    Symbol* findSymbol(const std::string& name);
    bool symbolExistsInCurrentScope(const std::string& name);

    // Type checking
    bool isValidType(const std::string& type);
    bool typesAreCompatible(const std::string& type1, const std::string& type2);
    std::string typeToString(Type* type);
    std::string getExpressionType(Expr* expr);
    bool isValidAssignmentTarget(Expr* expr);
    bool isSubclassOf(const std::string& derived, const std::string& base);
    const ClassSymbol* classOf(const std::string& type);
    const FunctionSymbol* findMethod(const std::string& cls, const std::string& name);
    const Symbol* findAttribute(const std::string& cls, const std::string& name);
    bool classFullyKnown(const std::string& cls);
    std::string elementTypeOf(const std::string& container);
    std::unique_ptr<Type> typeFromString(const std::string& type);

    // Calls: arity and argument types against a signature
    void checkCallArguments(CallExpr* call, const std::string& what, const FunctionSymbol& fs,
                            int name_length);
    void checkMethodCall(CallExpr* call, MemberAccess* callee);
    void checkSuper(CallExpr* super_call);
    bool isSuperCall(Expr* expr);
    void checkIntegerTarget(Expr* value, const std::string& target_type, const std::string& what);

    // `x = value` with no declaration in scope declares x (type inference)
    void inferDeclaration(std::unique_ptr<Stmt>& stmt);
    void analyzeBody(std::vector<std::unique_ptr<Stmt>>& body, bool guarded = false);
    bool terminates(const std::vector<std::unique_ptr<Stmt>>& body);
    bool terminates(Stmt* stmt);

    // Analysis methods
    void analyzeProgram(Program* program);
    void analyzeStatement(Stmt* stmt);
    void analyzeGuarded(Stmt* stmt);                  // [diag-fix] records an error and keeps going
    std::vector<pycpp::Diagnostic> collected;
    void checkUnreachable(const std::vector<std::unique_ptr<Stmt>>& body);   // [fix2]
    void analyzeVarDecl(VarDecl* var);
    void analyzeFunction(FunctionDef* func);
    void analyzeClass(ClassDef* cls);
    void analyzeStruct(StructDef* struc);
    void analyzeExpression(Expr* expr);
    void analyzeIfStmt(IfStmt* if_stmt);
    void analyzeWhileStmt(WhileStmt* while_stmt);
    void analyzeForStmt(ForStmt* for_stmt);
    void analyzeReturnStmt(ReturnStmt* return_stmt);

    void analyzeDeferStmt(DeferStmt* d);
    void analyzeMatchStmt(MatchStmt* m);
    void analyzeTryStmt(TryStmt* t);
    void analyzeRaiseStmt(RaiseStmt* r);
    int handler_depth = 0;   // > 0 while analysing the body of an except clause

    // NEW: Import statement analysis
    void analyzeImportStmt(ImportStmt* import);
    void bindModuleName(const std::string& name, ImportStmt* import);

    // NEW: Extract member variables from constructors
    void extractMembersFromConstructor(FunctionDef* constructor, ClassDef* cls);

    // Built-in usage tracking
    void trackBuiltinUsage(const std::string& builtin);

    // Error reporting
    void error(const char* code, const std::string& message, int line, int column, const std::string& hint = "", int length = 1);
    void warning(const char* code, const std::string& message, int line, int column, const std::string& hint = "", int length = 1);
    void errorAt(const char* code, const std::string& message, const ASTNode* node, const std::string& hint = "");
    void warningAt(const char* code, const std::string& message, const ASTNode* node, const std::string& hint = "");

public:
    SemanticAnalyzer();
    void analyze(std::unique_ptr<Program>& program);
    // Classes and functions of analysed modules are remembered under their module's
    // generated name, so a module that imports them can check calls against them.
    void setModuleStem(const std::string& stem) { module_stem = stem; }
    static void resetModuleExports();

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