#ifndef CODE_GENERATOR_H
#define CODE_GENERATOR_H

#include "ast.h"
#include "include_manager.h"
#include <sstream>
#include <memory>
#include <set>
#include <map>
#include <string>
#include <vector>

namespace pycpp {
// Filled for every module before any code is generated, so a class can know the
// members its ancestors declare (even when the base lives in another module).
void registerClasses(Program* program);
bool isInheritedMember(const std::string& class_name, const std::string& member);
bool hasSubclasses(const std::string& class_name);
bool hasMember(const std::string& class_name, const std::string& member);   // own or inherited
void resetClassRegistry();   // forget every class (used by --watch between runs)
bool& lineDirectives();   // [fix2] emit #line so C++ errors point at the .pycpp source
// [fix.py] declared type of a member (searches base classes); nullptr when unknown
Type* memberType(const std::string& class_name, const std::string& member);
}

class CodeGenerator : public ASTVisitor {
private:
    std::ostringstream output;
    int indent_level;
    FunctionDef* current_function;
    ClassDef* current_class;
    std::map<std::string, std::string> imported_modules; // module name -> its C++ namespace ("" for a bare package prefix)
    std::string module_namespace;                        // namespace this module's code is wrapped in

    // Declared types of the variables currently in scope (innermost scope last).
    // Used to decide things the syntax alone cannot tell, such as '.' vs '->'.
    std::vector<std::map<std::string, Type*>> scopes;
    bool header_mode = false;   // true: emit declarations for the .h file
    bool needs_utility = false; // set when generated code uses std::move
    int defer_counter = 0, match_counter = 0, unpack_counter = 0;
    bool class_is_polymorphic = false;
    bool subscript_write = false;   // the subscript being generated is the target of a plain '='

    // Builtins such as sorted()/enumerate()/input() expand to immediately-invoked
    // lambdas. Inside a function they must capture locals; at namespace scope a
    // capture-default is not allowed.
    const char* lambdaCapture() const { return current_function ? "[&]" : "[]"; }

    std::vector<std::string> handler_vars;   // names bound by the `except ... as e` clauses being generated
    int try_counter = 0;
    void useRuntime();          // generated code needs pycpp_runtime.h

    void pushScope();
    void popScope();
    void declareVar(const std::string& name, Type* type);
    Type* lookupVar(const std::string& name);
    // [fix.py] what the generator can tell about an expression's type
    struct TypeInfo {
        bool known = false;      // false: nothing could be determined
        int ptr = 0;             // pointer depth (Node* -> 1, Node** -> 2)
        std::string cls;         // class/struct name when it is one
        Type* elem = nullptr;    // element type when it is a list[T]
    };
    TypeInfo typeInfoOf(Type* type);
    TypeInfo exprTypeInfo(Expr* expr);
    void generateObjectOperand(Expr* expr);   // parenthesises (*p), a + b, ... before '.' / '->' / '['
    bool isPointerExpr(Expr* expr);
    bool isModuleExpr(Expr* expr);   // graphics, graphics.shapes, or an alias
    std::string moduleNamespaceOf(Expr* expr);   // C++ namespace of the module an expression names
    std::string exprToString(Expr* expr);
    bool usesSelf(Expr* expr);
    void generateTemplateHeader(const std::vector<std::string>& params);
    void generateParameters(FunctionDef* func, bool include_defaults);
    void generatePrototype(FunctionDef* func);
    void collectConstructorInitializers(FunctionDef* ctor, size_t start,
        std::vector<std::pair<std::string, std::string>>& inits, size_t& consumed_count);
    std::string formatDouble(double value);

    void indent();
    void dedent();
    std::string getIndent();

    int last_statement_line = 0; // source line of the previous statement, to keep blank lines
    IncludeManager* includes = nullptr; // set while generating; records headers the emitted code needs
    void require(const std::string& feature) { if (includes) includes->trackFeature(feature); }

    void generateProgram(Program* program, IncludeManager& include_manager);
    void generateStatement(Stmt* stmt);
    void generateFunction(FunctionDef* func);
    void generateClass(ClassDef* cls);
    void generateVarDecl(VarDecl* var);
    void generateType(Type* type);
    void generateExpression(Expr* expr);
    void generateRangeLoop(ForStmt& node, CallExpr* call);
    void generateForLoop(ForStmt& node);   // fix.py3:for-decl
    void generateComprehension(Comprehension* comp);
    void generateFString(FString* fs);
    void generateLambda(Lambda* lambda);
    std::string intLiteralText(long long value);
    bool isSuperCall(Expr* expr);
    bool isUserObject(Expr* expr);   // a PyCPP/C++ class object (not str/list/dict/set)

    void generateMemberInitializerList(FunctionDef* constructor, ClassDef* cls);
    void generateConstructorBody(FunctionDef* constructor, ClassDef* cls);
    void extractMemberVariablesFromConstructor(FunctionDef* constructor, ClassDef* cls);
    bool hasConstructorWithInitializers(ClassDef* cls);

    void analyzeProgramForIncludes(Program* program, IncludeManager& include_manager);

public:
    CodeGenerator();
    void setNamespace(const std::string& ns) { module_namespace = ns; }
    // Source file (.cpp): function definitions only.
    std::string generate(std::unique_ptr<Program>& program, IncludeManager& include_manager);
    // Header file (.h): classes, structs, templates, constants and function prototypes.
    std::string generateHeader(std::unique_ptr<Program>& program, IncludeManager& include_manager,
        const std::string& guard_name);

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
    void visit(ImportStmt& node) override;
    void visit(UseStmt& node) override;
    void visit(DeferStmt& node) override;
    void visit(MatchStmt& node) override;
    void visit(TryStmt& node) override;
    void visit(RaiseStmt& node) override;
    void visit(TupleLiteral& node) override;
    void visit(UnpackStmt& node) override;
    void visit(WithStmt& node) override;   // fix.py3:with-cg-decl

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
};

#endif