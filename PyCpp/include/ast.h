#ifndef AST_H
#define AST_H

#include <memory>
#include <vector>
#include <string>
#include <utility>

// Forward declarations
class ASTVisitor;
class Program;
class BasicType;
class PointerType;
class ListType;
class DictType;
class IntLiteral;
class FloatLiteral;
class StringLiteral;
class BoolLiteral;
class NoneLiteral;
class Identifier;
class BinaryOp;
class UnaryOp;
class TernaryOp;
class CallExpr;
class ListLiteral;
class DictLiteral;
class MemberAccess;
class ExprStmt;
class VarDecl;
class ReturnStmt;
class PassStmt;
class BreakStmt;
class ContinueStmt;
class IfStmt;
class WhileStmt;
class ForStmt;
class Block;
class UnsafeBlock;
class FunctionDef;
class ClassDef;
class StructDef;
class TemplateDef;
class ConstexprDef;
class ImportStmt;
class UseStmt;
class MatchStmt;
class DeferStmt;

// Base AST Node
class ASTNode {
public:
    int line = 0;
    int column = 0;
    virtual ~ASTNode() = default;
    virtual void accept(ASTVisitor& visitor) = 0;
};

// Base Type
class Type : public ASTNode {
public:
    std::string name;
    bool is_const = false;      // 'const T' / 'const &T'
    bool is_reference = false;  // '&T' (mutable) or 'const &T' (immutable)
};

class BasicType : public Type {
public:
    // Generic arguments, e.g. Container[int] -> type_args = { int }
    std::vector<std::unique_ptr<Type>> type_args;
    void accept(ASTVisitor& visitor) override;
};

class PointerType : public Type {
public:
    std::unique_ptr<Type> base_type;
    void accept(ASTVisitor& visitor) override;
};

class ListType : public Type {
public:
    std::unique_ptr<Type> element_type;
    void accept(ASTVisitor& visitor) override;
};

class DictType : public Type {
public:
    std::unique_ptr<Type> key_type;
    std::unique_ptr<Type> value_type;
    void accept(ASTVisitor& visitor) override;
};

// Base Expression
class Expr : public ASTNode {};

class IntLiteral : public Expr {
public:
    int value = 0;
    void accept(ASTVisitor& visitor) override;
};

class FloatLiteral : public Expr {
public:
    double value = 0.0;
    void accept(ASTVisitor& visitor) override;
};

class StringLiteral : public Expr {
public:
    std::string value;
    void accept(ASTVisitor& visitor) override;
};

class BoolLiteral : public Expr {
public:
    bool value = false;
    void accept(ASTVisitor& visitor) override;
};

class NoneLiteral : public Expr {
public:
    void accept(ASTVisitor& visitor) override;
};

class Identifier : public Expr {
public:
    std::string name;
    void accept(ASTVisitor& visitor) override;
};

class BinaryOp : public Expr {
public:
    std::unique_ptr<Expr> left;
    std::string op;
    std::unique_ptr<Expr> right;
    void accept(ASTVisitor& visitor) override;
};

class UnaryOp : public Expr {
public:
    std::string op;
    std::unique_ptr<Expr> operand;
    void accept(ASTVisitor& visitor) override;
};

class TernaryOp : public Expr {
public:
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Expr> then_expr;
    std::unique_ptr<Expr> else_expr;
    void accept(ASTVisitor& visitor) override;
};

class CallExpr : public Expr {
public:
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> arguments;
    // Explicit generic arguments: mem.alloc[Player](...), Container[int](42)
    std::vector<std::unique_ptr<Type>> type_args;
    // true for a[i] (indexing), false for f(x) (a call)
    bool is_subscript = false;
    void accept(ASTVisitor& visitor) override;
};

class ListLiteral : public Expr {
public:
    std::vector<std::unique_ptr<Expr>> elements;
    void accept(ASTVisitor& visitor) override;
};

class DictLiteral : public Expr {
public:
    std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> items;
    void accept(ASTVisitor& visitor) override;
};

class MemberAccess : public Expr {
public:
    std::unique_ptr<Expr> object;
    std::string member;
    MemberAccess(std::unique_ptr<Expr> obj, const std::string& mem, int ln, int col);
    void accept(ASTVisitor& visitor) override;
};

// Base Statement
class Stmt : public ASTNode {};

class ExprStmt : public Stmt {
public:
    std::unique_ptr<Expr> expr;
    void accept(ASTVisitor& visitor) override;
};

struct Parameter {
    std::string name;
    std::unique_ptr<Type> type;
    std::unique_ptr<Expr> default_value;

    Parameter(std::string n, std::unique_ptr<Type> t, std::unique_ptr<Expr> dv);
};

class VarDecl : public Stmt {
public:
    std::string var_name;
    std::unique_ptr<Type> type;
    std::unique_ptr<Expr> value;
    void accept(ASTVisitor& visitor) override;
};

class ReturnStmt : public Stmt {
public:
    std::unique_ptr<Expr> value;
    void accept(ASTVisitor& visitor) override;
};

class PassStmt : public Stmt {
public:
    bool is_ellipsis = false; // the '...' placeholder statement
    void accept(ASTVisitor& visitor) override;
};

class BreakStmt : public Stmt {
public:
    void accept(ASTVisitor& visitor) override;
};

class ContinueStmt : public Stmt {
public:
    void accept(ASTVisitor& visitor) override;
};

class IfStmt : public Stmt {
public:
    std::unique_ptr<Expr> condition;
    std::vector<std::unique_ptr<Stmt>> then_branch;
    std::vector<std::pair<std::unique_ptr<Expr>, std::vector<std::unique_ptr<Stmt>>>> elif_branches;
    std::vector<std::unique_ptr<Stmt>> else_branch;
    void accept(ASTVisitor& visitor) override;
};

class WhileStmt : public Stmt {
public:
    std::unique_ptr<Expr> condition;
    std::vector<std::unique_ptr<Stmt>> body;
    void accept(ASTVisitor& visitor) override;
};

class ForStmt : public Stmt {
public:
    std::string var_name;                // first loop variable
    std::vector<std::string> var_names;  // all loop variables: for k, v in ...
    std::unique_ptr<Expr> iterable;
    std::vector<std::unique_ptr<Stmt>> body;
    void accept(ASTVisitor& visitor) override;
};

class Block : public Stmt {
public:
    std::vector<std::unique_ptr<Stmt>> statements;
    void accept(ASTVisitor& visitor) override;
};

class UnsafeBlock : public Stmt {
public:
    std::vector<std::unique_ptr<Stmt>> body;
    void accept(ASTVisitor& visitor) override;
};

class FunctionDef : public Stmt {
public:
    std::string name;
    std::vector<std::string> type_params; // def f[T, U](...)
    std::vector<Parameter> parameters;
    std::unique_ptr<Type> return_type;
    std::vector<std::unique_ptr<Stmt>> body;
    void accept(ASTVisitor& visitor) override;
};

class ClassDef : public Stmt {
public:
    std::string name;
    std::vector<std::string> type_params; // class C[T]:
    std::string base_class;
    std::vector<std::unique_ptr<VarDecl>> attributes;
    std::vector<std::unique_ptr<FunctionDef>> methods;
    void accept(ASTVisitor& visitor) override;
};

class StructDef : public Stmt {
public:
    std::string name;
    std::vector<std::unique_ptr<VarDecl>> fields;
    void accept(ASTVisitor& visitor) override;
};

class TemplateDef : public Stmt {
public:
    std::string type_param;
    std::unique_ptr<ClassDef> templated_class;
    std::unique_ptr<FunctionDef> templated_function;
    void accept(ASTVisitor& visitor) override;
};

class ConstexprDef : public Stmt {
public:
    std::string name;
    std::unique_ptr<Type> type;
    std::unique_ptr<Expr> value;
    void accept(ASTVisitor& visitor) override;
};

class ImportStmt : public Stmt {
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
};

class UseStmt : public Stmt {
public:
    std::string library;
    void accept(ASTVisitor& visitor) override;
};

struct MatchCase {
    std::vector<std::unique_ptr<Expr>> patterns;   // empty when is_default
    bool is_default = false;                       // `case _:`
    std::vector<std::unique_ptr<Stmt>> body;
    int line = 0, column = 0;
};

class MatchStmt : public Stmt {
public:
    std::unique_ptr<Expr> subject;
    std::vector<MatchCase> cases;
    void accept(ASTVisitor& visitor) override;
};

class DeferStmt : public Stmt {
public:
    std::vector<std::unique_ptr<Stmt>> body;
    void accept(ASTVisitor& visitor) override;
};

// except [Type | (Type, Type)] [as name]:
struct ExceptClause {
    std::vector<std::string> type_names;   // empty: `except:` catches everything
    std::string var_name;                  // `as e`; empty when absent
    std::vector<std::unique_ptr<Stmt>> body;
    int line = 0, column = 0;
};

class TryStmt : public Stmt {
public:
    std::vector<std::unique_ptr<Stmt>> body;
    std::vector<ExceptClause> handlers;
    std::vector<std::unique_ptr<Stmt>> else_body;
    std::vector<std::unique_ptr<Stmt>> finally_body;
    void accept(ASTVisitor& visitor) override;
};

class RaiseStmt : public Stmt {
public:
    std::unique_ptr<Expr> value;           // null: bare `raise` (re-raise)
    void accept(ASTVisitor& visitor) override;
};

class Program : public ASTNode {
public:
    std::vector<std::unique_ptr<Stmt>> statements;
    void accept(ASTVisitor& visitor) override;
};

// Visitor interface
class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;

    // Program
    virtual void visit(Program& node) = 0;

    // Types
    virtual void visit(BasicType& node) = 0;
    virtual void visit(PointerType& node) = 0;
    virtual void visit(ListType& node) = 0;
    virtual void visit(DictType& node) = 0;

    // Expressions
    virtual void visit(IntLiteral& node) = 0;
    virtual void visit(FloatLiteral& node) = 0;
    virtual void visit(StringLiteral& node) = 0;
    virtual void visit(BoolLiteral& node) = 0;
    virtual void visit(NoneLiteral& node) = 0;
    virtual void visit(Identifier& node) = 0;
    virtual void visit(BinaryOp& node) = 0;
    virtual void visit(UnaryOp& node) = 0;
    virtual void visit(TernaryOp& node) = 0;
    virtual void visit(CallExpr& node) = 0;
    virtual void visit(ListLiteral& node) = 0;
    virtual void visit(DictLiteral& node) = 0;
    virtual void visit(MemberAccess& node) = 0;

    // Statements
    virtual void visit(ExprStmt& node) = 0;
    virtual void visit(VarDecl& node) = 0;
    virtual void visit(ReturnStmt& node) = 0;
    virtual void visit(PassStmt& node) = 0;
    virtual void visit(BreakStmt& node) = 0;
    virtual void visit(ContinueStmt& node) = 0;
    virtual void visit(IfStmt& node) = 0;
    virtual void visit(WhileStmt& node) = 0;
    virtual void visit(ForStmt& node) = 0;
    virtual void visit(Block& node) = 0;
    virtual void visit(UnsafeBlock& node) = 0;
    virtual void visit(FunctionDef& node) = 0;
    virtual void visit(ClassDef& node) = 0;
    virtual void visit(StructDef& node) = 0;
    virtual void visit(TemplateDef& node) = 0;
    virtual void visit(ConstexprDef& node) = 0;
    virtual void visit(ImportStmt& node) = 0;
    virtual void visit(UseStmt& node) = 0;

    // Non-pure so visitors that don't care keep compiling
    virtual void visit(MatchStmt&) {}
    virtual void visit(DeferStmt&) {}
    virtual void visit(TryStmt&) {}
    virtual void visit(RaiseStmt&) {}
};

#endif