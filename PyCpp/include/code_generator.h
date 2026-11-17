#ifndef CODE_GENERATOR_H
#define CODE_GENERATOR_H

#include "ast.h"
#include "include_manager.h"
#include <sstream>
#include <memory>
#include <set>
#include <string>

class CodeGenerator : public ASTVisitor {
private:
    std::ostringstream output;
    int indent_level;
    FunctionDef* current_function;
    ClassDef* current_class;
    std::set<std::string> imported_modules; // identifiers that name an imported module

    void indent();
    void dedent();
    std::string getIndent();

    void generateProgram(Program* program, IncludeManager& include_manager);
    void generateStatement(Stmt* stmt);
    void generateFunction(FunctionDef* func);
    void generateClass(ClassDef* cls);
    void generateVarDecl(VarDecl* var);
    void generateType(Type* type);
    void generateExpression(Expr* expr);

    void generateMemberInitializerList(FunctionDef* constructor, ClassDef* cls);
    void generateConstructorBody(FunctionDef* constructor, ClassDef* cls);
    void extractMemberVariablesFromConstructor(FunctionDef* constructor, ClassDef* cls);
    bool hasConstructorWithInitializers(ClassDef* cls);

    void analyzeProgramForIncludes(Program* program, IncludeManager& include_manager);

public:
    CodeGenerator();
    std::string generate(std::unique_ptr<Program>& program, IncludeManager& include_manager);

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