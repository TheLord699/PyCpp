#include "../include/ast.h"

// ===== Parameter Implementation =====
Parameter::Parameter(std::string n, std::unique_ptr<Type> t, std::unique_ptr<Expr> dv)
    : name(std::move(n)), type(std::move(t)), default_value(std::move(dv)) {
}

// ===== MemberAccess Implementation =====
MemberAccess::MemberAccess(std::unique_ptr<Expr> obj, const std::string& mem, int ln, int col)
    : object(std::move(obj)), member(mem) {
    line = ln;
    column = col;
}

void MemberAccess::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

// ===== AST Node Accept Method Implementations =====

// Program
void Program::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

// ===== Type Accept Methods =====

void BasicType::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void PointerType::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void ListType::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void DictType::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

// ===== Expression Accept Methods =====

void IntLiteral::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void FloatLiteral::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void StringLiteral::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void BoolLiteral::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void NoneLiteral::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void Identifier::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void BinaryOp::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void UnaryOp::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void TernaryOp::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void CallExpr::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void ListLiteral::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void DictLiteral::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

// ===== Statement Accept Methods =====

void ExprStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void VarDecl::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void ReturnStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void PassStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void BreakStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void ContinueStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void IfStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void WhileStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void ForStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void Block::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void UnsafeBlock::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void FunctionDef::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void ClassDef::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void StructDef::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void TemplateDef::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void ConstexprDef::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void ImportStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void UseStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void MatchStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void TryStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void RaiseStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void DeferStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void Comprehension::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void FString::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void Lambda::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void TupleLiteral::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

void UnpackStmt::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}
