#include "../include/log.h"
#include "../include/code_generator.h"
#include "../include/header_generator.h"
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <stdexcept>

// ---- Class registry (inheritance across modules) ----
namespace {
struct ClassInfo { std::string base; std::set<std::string> members; std::map<std::string, Type*> types; /* [fix.py] */ };
std::map<std::string, ClassInfo>& classRegistry() {
    static std::map<std::string, ClassInfo> registry;
    return registry;
}
std::string selfMemberAssigned(Stmt* stmt) {
    if (auto v = dynamic_cast<VarDecl*>(stmt))
        return v->var_name.rfind("self.", 0) == 0 ? v->var_name.substr(5) : "";
    if (auto es = dynamic_cast<ExprStmt*>(stmt))
        if (auto bin = dynamic_cast<BinaryOp*>(es->expr.get()); bin && bin->op == "=")
            if (auto m = dynamic_cast<MemberAccess*>(bin->left.get()))
                if (auto id = dynamic_cast<Identifier*>(m->object.get()); id && id->name == "self")
                    return m->member;
    return "";
}
}

void pycpp::registerClasses(Program* program) {
    // [fix.py] Also records the declared type of every member (and covers structs), so the
    // code generator can tell which member accesses go through a pointer and need '->'.
    for (auto& stmt : program->statements) {
        if (auto st = dynamic_cast<StructDef*>(stmt.get())) {
            ClassInfo& sinfo = classRegistry()[st->name];
            for (auto& f : st->fields) {
                sinfo.members.insert(f->var_name);
                if (f->type) sinfo.types[f->var_name] = f->type.get();
            }
            continue;
        }
        auto cls = dynamic_cast<ClassDef*>(stmt.get());
        if (!cls) continue;
        ClassInfo& info = classRegistry()[cls->name];
        info.base = cls->base_class;
        for (auto& attr : cls->attributes) {
            std::string n = attr->var_name;
            if (n.rfind("self.", 0) == 0) n = n.substr(5);
            info.members.insert(n);
            if (attr->type) info.types[n] = attr->type.get();
        }
        for (auto& m : cls->methods) {
            if (m->name != "__init__") continue;
            for (auto& s : m->body) {
                std::string n = selfMemberAssigned(s.get());
                if (n.empty()) continue;
                info.members.insert(n);
                if (auto v = dynamic_cast<VarDecl*>(s.get()))
                    if (v->type && !info.types.count(n)) info.types[n] = v->type.get();
            }
        }
    }
}

// [fix.py] declared type of class/struct member `member`, searching base classes
Type* pycpp::memberType(const std::string& cls, const std::string& member) {
    auto& reg = classRegistry();
    std::string current = cls;
    for (int depth = 0; depth < 64; ++depth) {
        auto it = reg.find(current);
        if (it == reg.end()) return nullptr;
        auto found = it->second.types.find(member);
        if (found != it->second.types.end()) return found->second;
        if (it->second.base.empty()) return nullptr;
        current = it->second.base;
    }
    return nullptr;
}

bool pycpp::isInheritedMember(const std::string& cls, const std::string& member) {
    auto& reg = classRegistry();
    auto it = reg.find(cls);
    for (int depth = 0; it != reg.end() && !it->second.base.empty() && depth < 64; ++depth) {
        it = reg.find(it->second.base);
        if (it != reg.end() && it->second.members.count(member)) return true;
    }
    return false;
}

bool pycpp::hasSubclasses(const std::string& cls) {
    for (auto& entry : classRegistry()) if (entry.second.base == cls) return true;
    return false;
}

// IncludeAnalyzer class definition
class IncludeAnalyzer : public ASTVisitor {
private:
    IncludeManager& include_manager;

public:
    IncludeAnalyzer(IncludeManager& manager) : include_manager(manager) {}

    void visit(FunctionDef& node) override {
        // Analyze function return type and parameters
        if (node.return_type) {
            node.return_type->accept(*this);
        }
        for (auto& param : node.parameters) {
            if (param.type) {
                param.type->accept(*this);
            }
        }
        // Analyze function body
        for (auto& stmt : node.body) {
            stmt->accept(*this);
        }
    }

    void visit(ClassDef& node) override {
        // Analyze class attributes and methods
        for (auto& attr : node.attributes) {
            attr->accept(*this);
        }
        for (auto& method : node.methods) {
            method->accept(*this);
        }
    }

    void visit(VarDecl& node) override {
        // Analyze variable type and value
        if (node.type) {
            node.type->accept(*this);
        }
        if (node.value) {
            node.value->accept(*this);
        }
    }

    void visit(CallExpr& node) override {
        // Detect built-in function calls
        if (auto ident = dynamic_cast<Identifier*>(node.callee.get())) {
            std::string func_name = ident->name;

            if (func_name == "print" || func_name == "input") {
                include_manager.trackFeature("iostream");
                include_manager.trackFeature("string");
            }
            else if (func_name == "min" || func_name == "max" || func_name == "sorted" ||
                func_name == "reversed" || func_name == "enumerate" || func_name == "sort" ||
                func_name == "reverse" || func_name == "random_shuffle") {
                include_manager.trackFeature("algorithm");
            }
            else if (func_name == "abs" || func_name == "pow" || func_name == "round" ||
                func_name == "ceil" || func_name == "floor" || func_name == "sqrt" ||
                func_name == "sin" || func_name == "cos" || func_name == "tan" ||
                func_name == "log" || func_name == "log10" || func_name == "exp") {
                include_manager.trackFeature("cmath");
            }
            else if (func_name == "sum" || func_name == "accumulate") {
                include_manager.trackFeature("numeric");
            }
            else if (func_name == "vector" || func_name == "list" || func_name == "map" ||
                func_name == "set" || func_name == "tuple") {
                include_manager.trackFeature("vector");
                include_manager.trackFeature("map");
                include_manager.trackFeature("set");
                include_manager.trackFeature("tuple");
            }
            else if (func_name == "rand" || func_name == "randint" || func_name == "srand") {
                include_manager.trackFeature("cstdlib");
                include_manager.trackFeature("ctime");
            }
            else if (func_name == "bin" || func_name == "oct" || func_name == "hex") {
                include_manager.trackFeature("sstream");
                include_manager.trackFeature("iomanip");
            }
            else if (func_name == "malloc" || func_name == "free" || func_name == "mem.alloc" || func_name == "mem.free") {
                include_manager.trackFeature("cstdlib");
            }
        }

        // Analyze arguments
        for (auto& arg : node.arguments) {
            arg->accept(*this);
        }
    }

    void visit(BasicType& node) override {
        // Detect string type usage
        if (node.name == "str" || node.name == "std::string") {
            include_manager.trackFeature("string");
        }
        // Detect container types
        else if (node.name.find("vector") != std::string::npos ||
            node.name.find("std::vector") != std::string::npos) {
            include_manager.trackFeature("vector");
        }
        else if (node.name.find("map") != std::string::npos ||
            node.name.find("std::map") != std::string::npos) {
            include_manager.trackFeature("map");
        }
    }

    void visit(ListType& node) override {
        include_manager.trackFeature("vector");
        if (node.element_type) {
            node.element_type->accept(*this);
        }
    }

    void visit(DictType& node) override {
        include_manager.trackFeature("map");
        if (node.key_type) {
            node.key_type->accept(*this);
        }
        if (node.value_type) {
            node.value_type->accept(*this);
        }
    }

    void visit(StringLiteral& node) override {
        include_manager.trackFeature("string");
    }

    void visit(BinaryOp& node) override {
        // Detect compound operators that might need special handling
        if (node.op == "+=" || node.op == "-=" || node.op == "*=" || node.op == "/=") {
            include_manager.trackFeature("compound_ops");
        }

        // Analyze both sides
        if (node.left) node.left->accept(*this);
        if (node.right) node.right->accept(*this);
    }

    // Default implementations for other node types
    void visit(Program& node) override {}
    void visit(StructDef& node) override {}
    void visit(TemplateDef& node) override {}
    void visit(ConstexprDef& node) override {}
    void visit(ReturnStmt& node) override {
        if (node.value) node.value->accept(*this);
    }
    void visit(PassStmt& node) override {}
    void visit(BreakStmt& node) override {}
    void visit(ContinueStmt& node) override {}
    void visit(IfStmt& node) override {
        if (node.condition) node.condition->accept(*this);
        for (auto& stmt : node.then_branch) stmt->accept(*this);
        for (auto& pair : node.elif_branches) {
            if (pair.first) pair.first->accept(*this);
            for (auto& stmt : pair.second) stmt->accept(*this);
        }
        for (auto& stmt : node.else_branch) stmt->accept(*this);
    }
    void visit(WhileStmt& node) override {
        if (node.condition) node.condition->accept(*this);
        for (auto& stmt : node.body) stmt->accept(*this);
    }
    void visit(ForStmt& node) override {
        if (node.iterable) node.iterable->accept(*this);
        for (auto& stmt : node.body) stmt->accept(*this);
    }
    void visit(UnsafeBlock& node) override {
        for (auto& stmt : node.body) stmt->accept(*this);
    }
    void visit(ExprStmt& node) override {
        if (node.expr) node.expr->accept(*this);
    }
    void visit(Block& node) override {
        for (auto& stmt : node.statements) stmt->accept(*this);
    }
    void visit(IntLiteral& node) override {}
    void visit(FloatLiteral& node) override {}
    void visit(BoolLiteral& node) override {}
    void visit(NoneLiteral& node) override {}
    void visit(Identifier& node) override {}
    void visit(UnaryOp& node) override {
        if (node.operand) node.operand->accept(*this);
    }
    void visit(TernaryOp& node) override {
        if (node.condition) node.condition->accept(*this);
        if (node.then_expr) node.then_expr->accept(*this);
        if (node.else_expr) node.else_expr->accept(*this);
    }
    void visit(ListLiteral& node) override {
        for (auto& elem : node.elements) elem->accept(*this);
    }
    void visit(DictLiteral& node) override {
        for (auto& pair : node.items) {
            pair.first->accept(*this);
            pair.second->accept(*this);
        }
    }
    void visit(MemberAccess& node) override {
        if (node.object) node.object->accept(*this);
    }
    void visit(PointerType& node) override {
        if (node.base_type) node.base_type->accept(*this);
    }

    void visit(ImportStmt& node) override {
        // Track imported modules for header generation
        include_manager.trackManualInclude("\"" + node.module + ".h\"");
    }

    void visit(UseStmt& node) override {}
    void visit(DeferStmt& n) override { for (auto& s : n.body) s->accept(*this); }
    void visit(MatchStmt& n) override {
        if (n.subject) n.subject->accept(*this);
        for (auto& c : n.cases) for (auto& s : c.body) s->accept(*this);
    }
};

// CodeGenerator implementation
// Marks a source blank line in the generated text (resolved in generateProgram).
static const char kBlankMarker = '\x01';

// Text of a PyCPP string literal -> body of a C++ string literal.
static std::string cppEscape(const std::string& raw) {
    std::string out;
    for (unsigned char c : raw) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default:
            if (c < 0x20 || c == 0x7f) {
                out += '\\';
                out += static_cast<char>('0' + ((c >> 6) & 7));
                out += static_cast<char>('0' + ((c >> 3) & 7));
                out += static_cast<char>('0' + (c & 7));
            }
            else {
                out += static_cast<char>(c);
            }
        }
    }
    return out;
}

// Exceptions the runtime provides (user classes may derive from them)
static bool isBuiltinExceptionName(const std::string& name) {
    static const std::set<std::string> names = {
        "Exception", "ArithmeticError", "ZeroDivisionError", "OverflowError",
        "LookupError", "IndexError", "KeyError", "ValueError", "TypeError", "RuntimeError"
    };
    return names.count(name) > 0;
}

CodeGenerator::CodeGenerator() : indent_level(0), current_function(nullptr), current_class(nullptr) {
    scopes.emplace_back(); // global scope
}

// ---- Scoped variable types ----

void CodeGenerator::useRuntime() {
    if (includes) includes->trackManualInclude("\"pycpp_runtime.h\"");
}

void CodeGenerator::pushScope() { scopes.emplace_back(); }

void CodeGenerator::popScope() {
    if (scopes.size() > 1) scopes.pop_back();
}

void CodeGenerator::declareVar(const std::string& name, Type* type) {
    scopes.back()[name] = type;
}

Type* CodeGenerator::lookupVar(const std::string& name) {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) return found->second;
    }
    return nullptr;
}

// True when the expression is a variable declared as a pointer (*T), so that
// member access must use '->' instead of '.'.
// [fix.py] Type facts about an expression: pointer depth, class name, list element type.
CodeGenerator::TypeInfo CodeGenerator::typeInfoOf(Type* type) {
    TypeInfo info;
    if (!type) return info;
    Type* cur = type;
    while (auto p = dynamic_cast<PointerType*>(cur)) {
        info.ptr++;
        cur = p->base_type.get();
        if (!cur) return info;
    }
    info.known = true;
    if (auto basic = dynamic_cast<BasicType*>(cur)) info.cls = basic->name;
    else if (auto list = dynamic_cast<ListType*>(cur)) info.elem = list->element_type.get();
    return info;
}

CodeGenerator::TypeInfo CodeGenerator::exprTypeInfo(Expr* expr) {
    TypeInfo none;
    if (!expr) return none;

    if (auto ident = dynamic_cast<Identifier*>(expr)) {
        if ((ident->name == "self" || ident->name == "this") && current_class) {
            TypeInfo self;
            self.known = true;
            self.ptr = 1;
            self.cls = current_class->name;
            return self;
        }
        return typeInfoOf(lookupVar(ident->name));
    }
    if (auto member = dynamic_cast<MemberAccess*>(expr)) {
        TypeInfo object = exprTypeInfo(member->object.get());
        if (!object.known || object.cls.empty()) return none;
        return typeInfoOf(pycpp::memberType(object.cls, member->member));
    }
    if (auto unary = dynamic_cast<UnaryOp*>(expr)) {
        TypeInfo inner = exprTypeInfo(unary->operand.get());
        if (!inner.known) return none;
        if (unary->op == "*" && inner.ptr > 0) { inner.ptr--; return inner; }   // *p
        if (unary->op == "&") { inner.ptr++; return inner; }                    // &x
        return none;
    }
    if (auto call = dynamic_cast<CallExpr*>(expr)) {
        if (call->is_subscript) {                                               // a[i]
            TypeInfo base = exprTypeInfo(call->callee.get());
            if (!base.known) return none;
            if (base.ptr > 0) { base.ptr--; return base; }
            if (base.elem) return typeInfoOf(base.elem);
        }
        return none;
    }
    return none;
}

// True when the expression is a pointer, so member access must use '->' instead of '.'.
// Works for variables, self, chained members (a.next.next), *p, &x and list elements.
bool CodeGenerator::isPointerExpr(Expr* expr) {
    TypeInfo info = exprTypeInfo(expr);
    return info.known && info.ptr > 0;
}

// Operand of '.', '->' or '[': (*p).x, (a + b).x need their parentheses back,
// because the parser drops them.
void CodeGenerator::generateObjectOperand(Expr* expr) {
    const bool wrap = dynamic_cast<UnaryOp*>(expr) || dynamic_cast<BinaryOp*>(expr) ||
                      dynamic_cast<TernaryOp*>(expr);
    if (wrap) output << "(";
    generateExpression(expr);
    if (wrap) output << ")";
}

// Render an expression to a string without touching the main output.
std::string CodeGenerator::exprToString(Expr* expr) {
    std::ostringstream saved;
    saved.swap(output);
    generateExpression(expr);
    std::string result = output.str();
    output.swap(saved);
    return result;
}

bool CodeGenerator::usesSelf(Expr* expr) {
    if (!expr) return false;
    if (auto ident = dynamic_cast<Identifier*>(expr)) return ident->name == "self";
    if (auto bin = dynamic_cast<BinaryOp*>(expr)) return usesSelf(bin->left.get()) || usesSelf(bin->right.get());
    if (auto un = dynamic_cast<UnaryOp*>(expr)) return usesSelf(un->operand.get());
    if (auto mem = dynamic_cast<MemberAccess*>(expr)) return usesSelf(mem->object.get());
    if (auto tern = dynamic_cast<TernaryOp*>(expr)) {
        return usesSelf(tern->condition.get()) || usesSelf(tern->then_expr.get()) || usesSelf(tern->else_expr.get());
    }
    if (auto call = dynamic_cast<CallExpr*>(expr)) {
        if (usesSelf(call->callee.get())) return true;
        for (auto& arg : call->arguments) if (usesSelf(arg.get())) return true;
        return false;
    }
    if (auto list = dynamic_cast<ListLiteral*>(expr)) {
        for (auto& e : list->elements) if (usesSelf(e.get())) return true;
        return false;
    }
    if (auto dict = dynamic_cast<DictLiteral*>(expr)) {
        for (auto& kv : dict->items) if (usesSelf(kv.first.get()) || usesSelf(kv.second.get())) return true;
        return false;
    }
    return false;
}

// Shortest text that reads back as the same double, always with a '.' or exponent
// so that 10.0 stays a floating-point literal in C++.
std::string CodeGenerator::formatDouble(double value) {
    std::ostringstream text;
    for (int precision = 1; precision <= 17; ++precision) {
        text.str("");
        text.precision(precision);
        text << value;
        if (std::stod(text.str()) == value) break;
    }
    std::string result = text.str();
    if (result.find_first_of(".eEn") == std::string::npos) result += ".0"; // 'n' = nan/inf
    return result;
}

void CodeGenerator::generateTemplateHeader(const std::vector<std::string>& params) {
    if (params.empty()) return;
    output << getIndent() << "template <";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) output << ", ";
        output << "typename " << params[i];
    }
    output << ">\n";
}

// Deep copy of a type tree.
static std::unique_ptr<Type> cloneType(Type* type) {
    if (!type) return nullptr;
    std::unique_ptr<Type> copy;
    if (auto basic = dynamic_cast<BasicType*>(type)) {
        auto t = std::make_unique<BasicType>();
        for (auto& arg : basic->type_args) t->type_args.push_back(cloneType(arg.get()));
        copy = std::move(t);
    }
    else if (auto ptr = dynamic_cast<PointerType*>(type)) {
        auto t = std::make_unique<PointerType>();
        t->base_type = cloneType(ptr->base_type.get());
        copy = std::move(t);
    }
    else if (auto list = dynamic_cast<ListType*>(type)) {
        auto t = std::make_unique<ListType>();
        t->element_type = cloneType(list->element_type.get());
        copy = std::move(t);
    }
    else if (auto dict = dynamic_cast<DictType*>(type)) {
        auto t = std::make_unique<DictType>();
        t->key_type = cloneType(dict->key_type.get());
        t->value_type = cloneType(dict->value_type.get());
        copy = std::move(t);
    }
    else {
        return nullptr;
    }
    copy->name = type->name;
    copy->is_const = type->is_const;
    copy->is_reference = type->is_reference;
    copy->line = type->line;
    copy->column = type->column;
    return copy;
}

std::string CodeGenerator::generate(std::unique_ptr<Program>& program, IncludeManager& include_manager) {
    output.str(""); // Clear any existing content
    header_mode = false;
    generateProgram(program.get(), include_manager);
    return output.str();
}

std::string CodeGenerator::generateHeader(std::unique_ptr<Program>& program, IncludeManager& include_manager,
    const std::string& guard_name) {
    output.str("");
    header_mode = true;
    output << "#ifndef " << guard_name << "\n";
    output << "#define " << guard_name << "\n\n";
    generateProgram(program.get(), include_manager);
    output << "\n#endif // " << guard_name << "\n";
    return output.str();
}

void CodeGenerator::indent() {
    indent_level++;
}

void CodeGenerator::dedent() {
    if (indent_level > 0) indent_level--;
}

std::string CodeGenerator::getIndent() {
    return std::string(indent_level * 4, ' '); // 4 spaces per indent
}

void CodeGenerator::analyzeProgramForIncludes(Program* program, IncludeManager& include_manager) {
    IncludeAnalyzer analyzer(include_manager);
    for (auto& stmt : program->statements) {
        stmt->accept(analyzer);
    }
}

void CodeGenerator::generateProgram(Program* program, IncludeManager& include_manager) {
    includes = &include_manager;
    // FIRST: Analyze the program to detect required features
    analyzeProgramForIncludes(program, include_manager);

    // Remember imported module names and the headers they need
    std::vector<std::string> import_includes;
    imported_modules.clear();
    for (auto& stmt : program->statements) {
        if (auto use_stmt = dynamic_cast<UseStmt*>(stmt.get())) {
            // `use <cmath>` is a request for that C++ header
            include_manager.trackManualInclude("<" + use_stmt->library + ">");
        }
        if (auto import_stmt = dynamic_cast<ImportStmt*>(stmt.get())) {
            import_includes.push_back("\"" + import_stmt->module + ".h\"");
            imported_modules.insert(import_stmt->module);
        }
    }

    // Generate the body FIRST, so that features discovered while generating it
    // (for example std::move needing <utility>) are known when includes are written.
    std::string prefix = output.str(); // include guard in header mode
    output.str("");

    for (auto& stmt : program->statements) {
        Stmt* node = stmt.get();

        if (header_mode) {
            if (auto func = dynamic_cast<FunctionDef*>(node)) {
                if (!func->type_params.empty()) {
                    func->accept(*this);          // templates must be fully visible in the header
                    output << "\n";
                }
                else if (func->name != "main") {
                    generatePrototype(func);      // everything else is declared only
                }
            }
            else if (dynamic_cast<ClassDef*>(node) || dynamic_cast<StructDef*>(node) ||
                dynamic_cast<TemplateDef*>(node) || dynamic_cast<ConstexprDef*>(node)) {
                node->accept(*this);
                output << "\n";
            }
            else if (auto var = dynamic_cast<VarDecl*>(node)) {
                output << "extern ";
                generateType(var->type.get());
                output << " " << var->var_name << ";\n";
            }
        }
        else {
            // Source file: classes, structs, templates and constants live in the header
            if (dynamic_cast<ClassDef*>(node) || dynamic_cast<StructDef*>(node) ||
                dynamic_cast<TemplateDef*>(node) || dynamic_cast<ConstexprDef*>(node)) {
                continue;
            }
            if (auto func = dynamic_cast<FunctionDef*>(node)) {
                if (!func->type_params.empty()) continue; // generic functions live in the header
            }
            node->accept(*this);
            output << "\n";
        }
    }

    std::string body = output.str();

    // Resolve blank-line markers
    {
        std::vector<std::string> lines;
        std::istringstream in(body);
        for (std::string line; std::getline(in, line);) lines.push_back(line);

        auto trimmed = [](const std::string& text) {
            size_t first = text.find_first_not_of(" \t");
            return first == std::string::npos ? std::string() : text.substr(first);
        };

        std::string resolved;
        std::string previous; // previous non-marker line, trimmed
        for (size_t i = 0; i < lines.size(); ++i) {
            if (!lines[i].empty() && lines[i][0] == kBlankMarker) {
                std::string next;
                for (size_t j = i + 1; j < lines.size(); ++j) {
                    if (lines[j].empty() || lines[j][0] != kBlankMarker) { next = trimmed(lines[j]); break; }
                }
                bool after_open = !previous.empty() && previous.back() == '{';
                bool before_close = !next.empty() && next[0] == '}';
                if (!after_open && !before_close) resolved += "\n";
                continue;
            }
            resolved += lines[i] + "\n";
            previous = trimmed(lines[i]);
        }
        body = resolved;
    }
    output.str("");

    if (needs_utility) include_manager.trackFeature("utility");

    // Headers follow from what was actually emitted, so the include list and the
    // code can never disagree (range() expands to std::vector, min() to std::min, ...).
    static const std::vector<std::pair<std::string, std::string>> emitted_to_header = {
        {"std::cout", "iostream"}, {"std::cin", "iostream"}, {"std::flush", "iostream"},
        {"std::string", "string"}, {"std::to_string", "string"}, {"std::stoi", "string"},
        {"std::stof", "string"}, {"std::getline", "string"},
        {"std::vector", "vector"}, {"std::map", "map"}, {"std::set", "set"},
        {"std::tuple", "tuple"}, {"std::make_tuple", "tuple"},
        {"std::stringstream", "sstream"}, {"std::hex", "iomanip"}, {"std::oct", "iomanip"},
        {"std::min", "algorithm"}, {"std::max", "algorithm"}, {"std::sort", "algorithm"},
        {"std::reverse", "algorithm"}, {"std::min_element", "algorithm"}, {"std::max_element", "algorithm"},
        {"std::accumulate", "numeric"}, {"std::decay_t", "type_traits"},
        {"std::shuffle", "algorithm"}, {"std::mt19937", "random"}, {"std::random_device", "random"},
        {"std::sqrt", "cmath"}, {"std::pow", "cmath"}, {"std::abs", "cmath"}, {"std::round", "cmath"},
        {"std::sin", "cmath"}, {"std::cos", "cmath"}, {"std::tan", "cmath"}, {"std::log", "cmath"},
        {"std::log10", "cmath"}, {"std::exp", "cmath"}, {"std::ceil", "cmath"}, {"std::floor", "cmath"},
        {"std::pair", "utility"}, {"std::make_pair", "utility"},
        {"std::function", "functional"}, {"std::unique_ptr", "memory"}, {"std::make_unique", "memory"},
        {"std::shared_ptr", "memory"}, {"std::make_shared", "memory"},
        {"int8_t", "cstdint"}, {"int16_t", "cstdint"}, {"int32_t", "cstdint"}, {"int64_t", "cstdint"},
    };
    for (const auto& entry : emitted_to_header) {
        if (body.find(entry.first) != std::string::npos) include_manager.trackFeature(entry.second);
    }

    output << prefix;

    // Generate includes from include manager
    auto includes = include_manager.getRequiredIncludes();
    for (const auto& include : includes) {
        // Generated headers sit next to their .cpp file, so a quoted local include is
        // reduced to its bare filename. This covers both path separators, e.g.
        // "C:\Users\...\temp\test_output.h" and "src/temp/test_output.h".
        std::string include_stmt = include;
        if (!include.empty() && include[0] == '"' && include.find_first_of("\\/") != std::string::npos) {
            size_t last_slash = include.find_last_of("\\/");
            size_t end_quote = include.find_last_of('"');
            if (last_slash != std::string::npos && end_quote != std::string::npos) {
                std::string filename = include.substr(last_slash + 1, end_quote - last_slash - 1);
                include_stmt = "\"" + filename + "\"";
            }
        }
        output << "#include " << include_stmt << "\n";
    }

    // Headers of imported PyCPP modules
    for (const auto& include : import_includes) {
        output << "#include " << include << "\n";
    }

    output << "\n" << body;
}

// Blank lines between statements in the source are kept in the output. A marker line
// is emitted here and resolved in generateProgram, where markers that would land right
// after an opening brace or before a closing one are dropped.

void CodeGenerator::generateStatement(Stmt* stmt) {
    if (stmt->line > 0 && last_statement_line > 0 && stmt->line > last_statement_line + 1) {
        output << kBlankMarker << "\n";
    }
    // Advance first so that nested statements measure their gap from this one's header
    if (stmt->line > last_statement_line) last_statement_line = stmt->line;
    stmt->accept(*this);
}

// Parameter list for a definition or prototype. Default values may appear only once
// in C++, so a free function's definition (in the .cpp) omits the ones its prototype has.
void CodeGenerator::generateParameters(FunctionDef* func, bool include_defaults) {
    bool first_param = true;
    for (const auto& param : func->parameters) {
        // 'self' is implicit in C++ member functions
        if (param.name == "self" && current_class) continue;

        if (!first_param) output << ", ";
        first_param = false;

        generateType(param.type.get());
        output << " " << param.name;

        if (param.default_value && include_defaults) {
            output << " = ";
            generateExpression(param.default_value.get());
        }
    }
}

// Declaration only: "int add(int a, int b);"
void CodeGenerator::generatePrototype(FunctionDef* func) {
    output << getIndent();
    generateType(func->return_type.get());
    output << " " << func->name << "(";
    generateParameters(func, true);
    output << ");\n";
}

// Collect the leading statements of a constructor that can become entries of the
// member initializer list: "self.x: T = expr" or "self.x = expr", where expr does not
// depend on 'self'. Only a leading run of such statements is taken, so statement
// order (and therefore any side effects) is unchanged.
void CodeGenerator::collectConstructorInitializers(FunctionDef* ctor,
    std::vector<std::pair<std::string, std::string>>& inits, size_t& consumed_count) {
    std::set<std::string> seen;
    consumed_count = 0;

    for (auto& stmt : ctor->body) {
        std::string member;
        Expr* value = nullptr;

        if (auto var_decl = dynamic_cast<VarDecl*>(stmt.get())) {
            if (var_decl->var_name.find("self.") != 0 || !var_decl->value) break;
            member = var_decl->var_name.substr(5);
            value = var_decl->value.get();
        }
        else if (auto expr_stmt = dynamic_cast<ExprStmt*>(stmt.get())) {
            auto bin_op = dynamic_cast<BinaryOp*>(expr_stmt->expr.get());
            if (!bin_op || bin_op->op != "=") break;
            auto member_access = dynamic_cast<MemberAccess*>(bin_op->left.get());
            if (!member_access) break;
            auto self_ident = dynamic_cast<Identifier*>(member_access->object.get());
            if (!self_ident || self_ident->name != "self") break;
            member = member_access->member;
            value = bin_op->right.get();
        }
        else {
            break;
        }

        // inherited members can't go in this class's initializer list
        if (current_class && pycpp::isInheritedMember(current_class->name, member)) break;
        if (usesSelf(value) || seen.count(member)) break;
        seen.insert(member);

        // A by-value parameter of a heavy type (string, list, dict) is moved into the
        // member instead of being copied.
        std::string init_expr;
        bool moved = false;
        if (auto ident = dynamic_cast<Identifier*>(value)) {
            for (const auto& param : ctor->parameters) {
                if (param.name != ident->name || !param.type) continue;
                Type* t = param.type.get();
                bool by_value = !t->is_reference && !dynamic_cast<PointerType*>(t);
                bool heavy = dynamic_cast<ListType*>(t) || dynamic_cast<DictType*>(t);
                if (auto basic = dynamic_cast<BasicType*>(t)) {
                    heavy = (basic->name == "std::string" || basic->name == "str");
                }
                if (by_value && heavy && !t->is_const) moved = true;
                break;
            }
        }
        if (moved) {
            init_expr = "std::move(" + exprToString(value) + ")";
            needs_utility = true;
        }
        else {
            init_expr = exprToString(value);
        }

        inits.emplace_back(member, init_expr);
        consumed_count++;
    }
}

void CodeGenerator::generateFunction(FunctionDef* func) {
    FunctionDef* previous_function = current_function;
    current_function = func;

    last_statement_line = func->line;
    const bool is_constructor = (func->name == "__init__" && current_class);
    const bool is_generic = !func->type_params.empty();
    // Methods live in the class body and templates in the header, so both keep their
    // default arguments; a free function's definition does not repeat them.
    const bool include_defaults = current_class != nullptr || is_generic;

    pushScope();
    for (const auto& param : func->parameters) {
        if (param.name == "self" && current_class) continue;
        declareVar(param.name, param.type.get());
    }

    generateTemplateHeader(func->type_params);
    output << getIndent();
    if (current_class && class_is_polymorphic && !is_constructor && !is_generic) output << "virtual ";

    if (is_constructor) {
        output << current_class->name; // constructors take the class name
    }
    else {
        generateType(func->return_type.get());
        output << " " << func->name;
    }

    output << "(";
    generateParameters(func, include_defaults);
    output << ")";

    std::vector<std::pair<std::string, std::string>> inits;
    size_t consumed = 0;
    if (is_constructor) {
        collectConstructorInitializers(func, inits, consumed);
        if (current_class && isBuiltinExceptionName(current_class->base_class)) {
            for (const auto& p : func->parameters) {
                if (p.name == "self" || !p.type) continue;
                auto basic = dynamic_cast<BasicType*>(p.type.get());
                if (basic && (basic->name == "std::string" || basic->name == "str"))
                    inits.insert(inits.begin(), { current_class->base_class, p.name });
                break;   // only the first real parameter can be the message
            }
        }
        if (!inits.empty()) {
            output << "\n" << getIndent() << "    : ";
            for (size_t i = 0; i < inits.size(); ++i) {
                if (i > 0) output << ", ";
                output << inits[i].first << "(" << inits[i].second << ")";
            }
        }
    }

    const size_t first_body_stmt = consumed;
    if (is_constructor && first_body_stmt >= func->body.size()) {
        output << " {}\n"; // everything went into the initializer list
        popScope();
        current_function = previous_function;
        return;
    }

    output << " {\n";
    indent();

    for (size_t i = first_body_stmt; i < func->body.size(); ++i) {
        generateStatement(func->body[i].get());
    }

    // Add implicit return for main function if missing
    if (func->name == "main" && func->return_type && func->return_type->name == "int") {
        bool has_return = false;
        for (auto& stmt : func->body) {
            if (dynamic_cast<ReturnStmt*>(stmt.get())) {
                has_return = true;
                break;
            }
        }
        if (!has_return) {
            output << getIndent() << "return 0;\n";
        }
    }

    dedent();
    output << getIndent() << "}\n";

    popScope();
    current_function = previous_function;
}

void CodeGenerator::extractMemberVariablesFromConstructor(FunctionDef* constructor, ClassDef* cls) {
    if (!constructor || !cls) return;

    // Track which member variables we've already processed from explicit attributes
    std::unordered_set<std::string> existing_members;
    for (auto& attr : cls->attributes) {
        std::string member_name = attr->var_name;
        if (member_name.find("self.") == 0) {
            member_name = member_name.substr(5);
        }
        existing_members.insert(member_name);
    }

    // Look for member variables in constructor body
    for (auto& stmt : constructor->body) {
        // Handle variable declarations like: self.name: str = name
        if (auto var_decl = dynamic_cast<VarDecl*>(stmt.get())) {
            if (var_decl->var_name.find("self.") == 0) {
                std::string member_name = var_decl->var_name.substr(5);

                if (existing_members.find(member_name) == existing_members.end()) {
                    // This is a new member variable declared in constructor
                    auto new_attr = std::make_unique<VarDecl>();
                    new_attr->var_name = var_decl->var_name;

                    // Copy the type (deep, so list[T], dict[K, V] and generics survive)
                    new_attr->type = cloneType(var_decl->type.get());
                    if (new_attr->type) {
                        new_attr->type->is_reference = false; // a member is never a reference
                    }

                    // Copy the value if present
                    if (var_decl->value) {
                        // For now, we'll just mark that there's a value without deep copying
                        // In a full implementation, you'd want to clone the expression tree
                        new_attr->value = nullptr; // Simplified
                    }

                    cls->attributes.push_back(std::move(new_attr));
                    existing_members.insert(member_name);
                    pycpp::debugLog() << "Extracted member variable from constructor: " << member_name << std::endl;
                }
            }
        }
        // Handle member assignments like: self.health = hp
        else if (auto expr_stmt = dynamic_cast<ExprStmt*>(stmt.get())) {
            if (auto bin_op = dynamic_cast<BinaryOp*>(expr_stmt->expr.get())) {
                if (bin_op->op == "=") {
                    if (auto member_access = dynamic_cast<MemberAccess*>(bin_op->left.get())) {
                        if (auto self_ident = dynamic_cast<Identifier*>(member_access->object.get())) {
                            if (self_ident->name == "self") {
                                std::string member_name = member_access->member;

                                if (existing_members.find(member_name) == existing_members.end()) {
                                    // This is a new member assignment, create a member variable
                                    auto new_attr = std::make_unique<VarDecl>();
                                    new_attr->var_name = "self." + member_name;

                                    // Try to infer type from the assignment
                                    if (auto right_ident = dynamic_cast<Identifier*>(bin_op->right.get())) {
                                        // Look for the parameter type
                                        for (const auto& param : constructor->parameters) {
                                            if (param.name == right_ident->name && param.type) {
                                                new_attr->type = cloneType(param.type.get());
                                                if (new_attr->type) {
                                                    // 'const &str' parameter -> plain std::string member
                                                    new_attr->type->is_reference = false;
                                                    new_attr->type->is_const = false;
                                                }
                                                break;
                                            }
                                        }
                                    }
                                    else if (dynamic_cast<IntLiteral*>(bin_op->right.get())) {
                                        auto int_type = std::make_unique<BasicType>();
                                        int_type->name = "int";
                                        new_attr->type = std::move(int_type);
                                    }
                                    else if (dynamic_cast<StringLiteral*>(bin_op->right.get())) {
                                        auto str_type = std::make_unique<BasicType>();
                                        str_type->name = "std::string";
                                        new_attr->type = std::move(str_type);
                                    }
                                    else if (dynamic_cast<FloatLiteral*>(bin_op->right.get())) {
                                        auto float_type = std::make_unique<BasicType>();
                                        float_type->name = "float";
                                        new_attr->type = std::move(float_type);
                                    }
                                    else if (dynamic_cast<BoolLiteral*>(bin_op->right.get())) {
                                        auto bool_type = std::make_unique<BasicType>();
                                        bool_type->name = "bool";
                                        new_attr->type = std::move(bool_type);
                                    }

                                    // If we couldn't determine the type, use auto
                                    if (!new_attr->type) {
                                        auto auto_type = std::make_unique<BasicType>();
                                        auto_type->name = "auto";
                                        new_attr->type = std::move(auto_type);
                                    }

                                    cls->attributes.push_back(std::move(new_attr));
                                    existing_members.insert(member_name);
                                    pycpp::debugLog() << "Inferred member variable from assignment: " << member_name << std::endl;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void CodeGenerator::generateClass(ClassDef* cls) {
    ClassDef* previous_class = current_class;
    current_class = cls;

    generateTemplateHeader(cls->type_params);
    const bool saved_poly = class_is_polymorphic;
    class_is_polymorphic = !cls->base_class.empty() || pycpp::hasSubclasses(cls->name);
    if (isBuiltinExceptionName(cls->base_class)) useRuntime();
    output << getIndent() << "class " << cls->name;

    if (!cls->base_class.empty()) {
        output << " : public " << cls->base_class;
    }

    output << " {\n";
    output << getIndent() << "public:\n";
    indent();

    // Extract member variables from constructors first
    for (auto& method : cls->methods) {
        if (method->name == "__init__") {
            extractMemberVariablesFromConstructor(method.get(), cls);
        }
    }

    // Generate member variable declarations from ALL attributes
    pycpp::debugLog() << "Generating class " << cls->name << " with " << cls->attributes.size() << " attributes" << std::endl;

    for (auto& attr : cls->attributes) {
        // Extract member name from "self.member_name" pattern
        std::string member_name = attr->var_name;
        if (member_name.find("self.") == 0) {
            member_name = member_name.substr(5); // Remove "self." prefix
        }

        // already declared by a base class: redeclaring would shadow it
        if (pycpp::isInheritedMember(cls->name, member_name)) continue;

        output << getIndent();
        generateType(attr->type.get());
        output << " " << member_name;

        // Only a default given in the class body itself is emitted; values assigned in
        // __init__ go through the constructor's initializer list.
        if (attr->value) {
            output << " = ";
            generateExpression(attr->value.get());
        }
        output << ";\n";
    }

    if (pycpp::hasSubclasses(cls->name))
        output << getIndent() << "virtual ~" << cls->name << "() = default;\n";

    // Generate methods, separated by blank lines
    bool first_method = true;
    for (auto& method : cls->methods) {
        if (first_method) {
            if (!cls->attributes.empty()) output << "\n";
            first_method = false;
        }
        else {
            output << "\n";
        }
        generateFunction(method.get());
    }

    dedent();
    output << getIndent() << "};\n";

    class_is_polymorphic = saved_poly;
    current_class = previous_class;
}

bool CodeGenerator::hasConstructorWithInitializers(ClassDef* cls) {
    for (auto& method : cls->methods) {
        if (method->name == "__init__") {
            return true;
        }
    }
    return false;
}

void CodeGenerator::generateVarDecl(VarDecl* var) {
    output << getIndent();

    // Check if it's a member variable (starts with "self.") 
    if (var->var_name.find("self.") == 0) {
        std::string member_name = var->var_name.substr(5);

        if (current_class && !current_function) {
            // In class context (not in method): generate as member variable declaration
            generateType(var->type.get());
            output << " " << member_name;

            if (var->value) {
                output << " = ";
                generateExpression(var->value.get());
            }
            output << ";\n";
        }
        else {
            // In method context: generate as member assignment
            output << "this->" << member_name;

            if (var->value) {
                output << " = ";
                generateExpression(var->value.get());
            }
            output << ";\n";
        }
    }
    else {
        // Regular variable declaration
        generateType(var->type.get());
        output << " " << var->var_name;

        if (var->value) {
            output << " = ";

            // `x: int = input(...)`: convert the entered text to the declared type
            if (auto call_expr = dynamic_cast<CallExpr*>(var->value.get())) {
                if (auto ident = dynamic_cast<Identifier*>(call_expr->callee.get())) {
                    if (ident->name == "input" && var->type) {
                        if (auto basic = dynamic_cast<BasicType*>(var->type.get())) {
                            static const std::set<std::string> ints = { "int", "int8_t", "int16_t", "int32_t", "int64_t" };
                            std::string conv;
                            if (ints.count(basic->name)) conv = "pycpp::to_int<" + basic->name + ">";
                            else if (basic->name == "float") conv = "pycpp::to_float";
                            else if (basic->name == "double") conv = "pycpp::to_double";
                            else if (basic->name == "bool") conv = "pycpp::parse_bool";
                            if (!conv.empty()) {
                                useRuntime();
                                output << conv << "(";
                                generateExpression(var->value.get());   // input() runs exactly once
                                output << ");\n";
                                return;
                            }
                        }
                    }
                }
            }

            // Default case - no special conversion needed
            generateExpression(var->value.get());
        }
        output << ";\n";
    }
}

void CodeGenerator::generateType(Type* type) {
    if (type && type->is_const) {
        output << "const ";
    }

    if (auto basic_type = dynamic_cast<BasicType*>(type)) {
        // Map PyCPP types to C++ types
        if (basic_type->name == "str" || basic_type->name == "std::string") {
            require("string");
            output << "std::string";
        }
        else if (basic_type->name == "None" || basic_type->name == "NoneType") {
            output << "void";
        }
        else {
            output << basic_type->name;
        }

        // Generic arguments: Container[int] -> Container<int>
        if (!basic_type->type_args.empty()) {
            output << "<";
            for (size_t i = 0; i < basic_type->type_args.size(); ++i) {
                if (i > 0) output << ", ";
                generateType(basic_type->type_args[i].get());
            }
            output << ">";
        }
    }
    else if (auto pointer_type = dynamic_cast<PointerType*>(type)) {
        generateType(pointer_type->base_type.get());
        output << "*";
    }
    else if (auto list_type = dynamic_cast<ListType*>(type)) {
        require("vector");
        output << "std::vector<";
        generateType(list_type->element_type.get());
        output << ">";
    }
    else if (auto dict_type = dynamic_cast<DictType*>(type)) {
        require("map");
        output << "std::map<";
        generateType(dict_type->key_type.get());
        output << ", ";
        generateType(dict_type->value_type.get());
        output << ">";
    }
    else {
        output << "auto";
    }

    if (type && type->is_reference) {
        output << "&";
    }
}

void CodeGenerator::generateExpression(Expr* expr) {
    if (!expr) {
        output << "/* null expression */";
        return;
    }

    // Handle CallExpr first - using the COMPREHENSIVE approach
    if (auto call_expr = dynamic_cast<CallExpr*>(expr)) {
        // a[i] is indexing, not a call
        if (call_expr->is_subscript) {
            generateObjectOperand(call_expr->callee.get());   // [fix.py]
            output << "[";
            for (size_t i = 0; i < call_expr->arguments.size(); ++i) {
                if (i > 0) output << ", ";
                generateExpression(call_expr->arguments[i].get());
            }
            output << "]";
            return;
        }

        // mem.alloc[T](args) -> new T(args), mem.free(p) -> delete p
        if (auto mem_member = dynamic_cast<MemberAccess*>(call_expr->callee.get())) {
            auto mem_ident = dynamic_cast<Identifier*>(mem_member->object.get());
            if (mem_ident && mem_ident->name == "mem" && !lookupVar("mem")) {
                if (mem_member->member == "alloc" && !call_expr->type_args.empty()) {
                    output << "new ";
                    generateType(call_expr->type_args[0].get());
                    output << "(";
                    for (size_t i = 0; i < call_expr->arguments.size(); ++i) {
                        if (i > 0) output << ", ";
                        generateExpression(call_expr->arguments[i].get());
                    }
                    output << ")";
                    return;
                }
                if (mem_member->member == "free" && call_expr->arguments.size() == 1) {
                    output << "delete ";
                    generateExpression(call_expr->arguments[0].get());
                    return;
                }
            }
        }

        // Name[T](args): explicit generic arguments -> Name<T>(args)
        if (!call_expr->type_args.empty()) {
            generateExpression(call_expr->callee.get());
            output << "<";
            for (size_t i = 0; i < call_expr->type_args.size(); ++i) {
                if (i > 0) output << ", ";
                generateType(call_expr->type_args[i].get());
            }
            output << ">(";
            for (size_t i = 0; i < call_expr->arguments.size(); ++i) {
                if (i > 0) output << ", ";
                generateExpression(call_expr->arguments[i].get());
            }
            output << ")";
            return;
        }

        // module.function(args): module functions are plain functions in C++
        if (auto module_member = dynamic_cast<MemberAccess*>(call_expr->callee.get())) {
            if (auto module_ident = dynamic_cast<Identifier*>(module_member->object.get())) {
                if (imported_modules.count(module_ident->name) > 0 && !lookupVar(module_ident->name)) {
                    output << module_member->member << "(";
                    for (size_t i = 0; i < call_expr->arguments.size(); ++i) {
                        if (i > 0) output << ", ";
                        generateExpression(call_expr->arguments[i].get());
                    }
                    output << ")";
                    return;
                }
            }
        }

        std::string function_name;

        // Extract function name whether it's direct call or member access
        if (auto ident = dynamic_cast<Identifier*>(call_expr->callee.get())) {
            function_name = ident->name;

            // Check if this might be a class constructor
            pycpp::debugLog() << "Generating call expression, might be constructor: " << function_name << std::endl;
        }
        else if (auto member_access = dynamic_cast<MemberAccess*>(call_expr->callee.get())) {
            if (auto obj_ident = dynamic_cast<Identifier*>(member_access->object.get())) {
                function_name = obj_ident->name + "." + member_access->member;
            }
            else {
                // Handle nested member access if needed
                function_name = "member_access"; // fallback
            }
        }

        // Handle built-in functions - COMPREHENSIVE with type conversions
        if (function_name == "assert") {
            require("assert"); // -> <cassert>
            if (call_expr->arguments.empty()) { output << "assert(true)"; return; }
            // Double parentheses keep commas inside templates from splitting the macro
            output << "assert((";
            generateExpression(call_expr->arguments[0].get());
            output << ")";
            if (call_expr->arguments.size() > 1) {
                output << " && ";
                generateExpression(call_expr->arguments[1].get());
            }
            output << ")";
            return;
        }
        else         if (function_name == "print") {
            require("iostream");
            output << "std::cout";
            for (size_t i = 0; i < call_expr->arguments.size(); ++i) {
                if (i > 0) output << " << ' '";
                useRuntime();
                output << " << pycpp::to_str(";
                generateExpression(call_expr->arguments[i].get());
                output << ")";
            }
            output << " << '\\n'";
            return;
        }
        else if (function_name == "input") {
            require("iostream");
            require("string");
            if (call_expr->arguments.size() > 0) {
                // Print prompt first, then get input
                output << lambdaCapture() << "() { ";
                output << "std::cout << ";
                generateExpression(call_expr->arguments[0].get());
                output << " << std::flush; ";
                output << "std::string input; ";
                output << "std::getline(std::cin, input); ";
                output << "return input; }()";
            }
            else {
                output << lambdaCapture() << "() { std::string input; std::getline(std::cin, input); return input; }()";
            }
            return;
        }
        else if (function_name == "len") {
            if (call_expr->arguments.size() > 0) {
                output << "(";
                generateExpression(call_expr->arguments[0].get());
                output << ").size()";
            }
            else {
                output << "0";
            }
            return;
        }
        else if (function_name == "int" || function_name == "float" ||
                 function_name == "str" || function_name == "bool") {
            if (call_expr->arguments.empty()) {
                if (function_name == "int") output << "0";
                else if (function_name == "float") output << "0.0f";
                else if (function_name == "str") { require("string"); output << "std::string()"; }
                else output << "false";
                return;
            }
            // int("42"), float("3.5"), str(x), bool(x): resolved by argument type in the runtime
            useRuntime();
            output << "pycpp::to_" << function_name << "(";
            generateExpression(call_expr->arguments[0].get());
            output << ")";
            return;
        }
        else if ((function_name == "list" || function_name == "set") && call_expr->arguments.size() == 1) {
            output << lambdaCapture() << "() { auto&& pycpp_c = ";
            generateExpression(call_expr->arguments[0].get());
            output << "; return std::" << (function_name == "list" ? "vector" : "set")
                   << "<std::decay_t<decltype(*pycpp_c.begin())>>(pycpp_c.begin(), pycpp_c.end()); }()";
            return;
        }
        else if ((function_name == "dict" || function_name == "list" || function_name == "set") &&
                 call_expr->arguments.empty()) {
            output << "{}";   // takes its type from the declaration it initialises
            return;
        }
        else if (function_name == "dict" && call_expr->arguments.size() == 1) {
            output << lambdaCapture() << "() { auto&& pycpp_c = ";
            generateExpression(call_expr->arguments[0].get());
            output << "; return std::decay_t<decltype(pycpp_c)>(pycpp_c); }()";
            return;
        }
        else if (function_name == "list") {
            if (call_expr->arguments.size() > 0) {
                output << "std::vector<decltype(";
                generateExpression(call_expr->arguments[0].get());
                output << ")>(";
                generateExpression(call_expr->arguments[0].get());
                output << ".begin(), ";
                generateExpression(call_expr->arguments[0].get());
                output << ".end())";
            }
            else {
                output << "std::vector<int>()";
            }
            return;
        }
        else if (function_name == "dict") {
            output << "std::map<auto, auto>()";
            return;
        }
        else if (function_name == "tuple") {
            if (call_expr->arguments.size() > 0) {
                output << "std::make_tuple(";
                for (size_t i = 0; i < call_expr->arguments.size(); ++i) {
                    generateExpression(call_expr->arguments[i].get());
                    if (i < call_expr->arguments.size() - 1) output << ", ";
                }
                output << ")";
            }
            else {
                output << "std::tuple<>()";
            }
            return;
        }
        else if (function_name == "set") {
            if (call_expr->arguments.size() > 0) {
                output << "std::set<decltype(*(";
                generateExpression(call_expr->arguments[0].get());
                output << ").begin())>(";
                generateExpression(call_expr->arguments[0].get());
                output << ".begin(), ";
                generateExpression(call_expr->arguments[0].get());
                output << ".end())";
            }
            else {
                output << "std::set<int>()";
            }
            return;
        }
        else if ((function_name == "min" || function_name == "max" || function_name == "sum") &&
                 call_expr->arguments.size() == 1) {
            // The argument is evaluated once. Calling it twice (for begin() and end())
            // would iterate two different temporaries, e.g. for range(5).
            output << lambdaCapture() << "() { auto&& pycpp_c = ";
            generateExpression(call_expr->arguments[0].get());
            if (function_name == "sum")
                output << "; return std::accumulate(pycpp_c.begin(), pycpp_c.end(), "
                          "std::decay_t<decltype(*pycpp_c.begin())>{}); }()";
            else
                output << "; return *std::" << function_name
                       << "_element(pycpp_c.begin(), pycpp_c.end()); }()";
            return;
        }
        else if (function_name == "min") {
            if (call_expr->arguments.size() == 1) {
                output << "*std::min_element((";
                generateExpression(call_expr->arguments[0].get());
                output << ").begin(), (";
                generateExpression(call_expr->arguments[0].get());
                output << ").end())";
            }
            else if (call_expr->arguments.size() > 1) {
                output << "std::min({";
                for (size_t i = 0; i < call_expr->arguments.size(); ++i) {
                    generateExpression(call_expr->arguments[i].get());
                    if (i < call_expr->arguments.size() - 1) output << ", ";
                }
                output << "})";
            }
            else {
                output << "0";
            }
            return;
        }
        else if (function_name == "max") {
            if (call_expr->arguments.size() == 1) {
                output << "*std::max_element((";
                generateExpression(call_expr->arguments[0].get());
                output << ").begin(), (";
                generateExpression(call_expr->arguments[0].get());
                output << ").end())";
            }
            else if (call_expr->arguments.size() > 1) {
                output << "std::max({";
                for (size_t i = 0; i < call_expr->arguments.size(); ++i) {
                    generateExpression(call_expr->arguments[i].get());
                    if (i < call_expr->arguments.size() - 1) output << ", ";
                }
                output << "})";
            }
            else {
                output << "0";
            }
            return;
        }
        else if (function_name == "sum") {
            if (call_expr->arguments.size() > 0) {
                output << "std::accumulate((";
                generateExpression(call_expr->arguments[0].get());
                output << ").begin(), (";
                generateExpression(call_expr->arguments[0].get());
                output << ").end(), 0)";
            }
            else {
                output << "0";
            }
            return;
        }
        else if (function_name == "abs") {
            if (call_expr->arguments.size() > 0) {
                output << "std::abs(";
                generateExpression(call_expr->arguments[0].get());
                output << ")";
            }
            else {
                output << "0";
            }
            return;
        }
        else if (function_name == "round") {
            if (call_expr->arguments.size() > 0) {
                output << "std::round(";
                generateExpression(call_expr->arguments[0].get());
                output << ")";
            }
            else {
                output << "0";
            }
            return;
        }
        else if (function_name == "pow") {
            if (call_expr->arguments.size() >= 2) {
                output << "std::pow(";
                generateExpression(call_expr->arguments[0].get());
                output << ", ";
                generateExpression(call_expr->arguments[1].get());
                output << ")";
            }
            else {
                output << "1";
            }
            return;
        }
        else if (function_name == "sqrt") {
            if (call_expr->arguments.size() > 0) {
                output << "std::sqrt(";
                generateExpression(call_expr->arguments[0].get());
                output << ")";
            }
            else {
                output << "0";
            }
            return;
        }
        else if (function_name == "sin") {
            if (call_expr->arguments.size() > 0) {
                output << "std::sin(";
                generateExpression(call_expr->arguments[0].get());
                output << ")";
            }
            else {
                output << "0";
            }
            return;
        }
        else if (function_name == "cos") {
            if (call_expr->arguments.size() > 0) {
                output << "std::cos(";
                generateExpression(call_expr->arguments[0].get());
                output << ")";
            }
            else {
                output << "1";
            }
            return;
        }
        else if (function_name == "tan") {
            if (call_expr->arguments.size() > 0) {
                output << "std::tan(";
                generateExpression(call_expr->arguments[0].get());
                output << ")";
            }
            else {
                output << "0";
            }
            return;
        }
        else if (function_name == "log") {
            if (call_expr->arguments.size() > 0) {
                output << "std::log(";
                generateExpression(call_expr->arguments[0].get());
                output << ")";
            }
            else {
                output << "0";
            }
            return;
        }
        else if (function_name == "log10") {
            if (call_expr->arguments.size() > 0) {
                output << "std::log10(";
                generateExpression(call_expr->arguments[0].get());
                output << ")";
            }
            else {
                output << "0";
            }
            return;
        }
        else if (function_name == "exp") {
            if (call_expr->arguments.size() > 0) {
                output << "std::exp(";
                generateExpression(call_expr->arguments[0].get());
                output << ")";
            }
            else {
                output << "1";
            }
            return;
        }
        else if (function_name == "ceil") {
            if (call_expr->arguments.size() > 0) {
                output << "std::ceil(";
                generateExpression(call_expr->arguments[0].get());
                output << ")";
            }
            else {
                output << "0";
            }
            return;
        }
        else if (function_name == "floor") {
            if (call_expr->arguments.size() > 0) {
                output << "std::floor(";
                generateExpression(call_expr->arguments[0].get());
                output << ")";
            }
            else {
                output << "0";
            }
            return;
        }
        else if (function_name == "bin" || function_name == "oct" || function_name == "hex") {
            if (call_expr->arguments.empty()) { output << "std::string(\"0\")"; return; }
            useRuntime();
            output << "pycpp::to_base(";
            generateExpression(call_expr->arguments[0].get());
            output << (function_name == "bin" ? ", 2, \"0b\")"
                     : function_name == "oct" ? ", 8, \"0o\")" : ", 16, \"0x\")");
            return;
        }
        else if (function_name == "bin") {
            if (call_expr->arguments.size() > 0) {
                output << lambdaCapture() << "() { std::stringstream ss; ss << std::hex << ";
                generateExpression(call_expr->arguments[0].get());
                output << "; return ss.str(); }()";
            }
            else {
                output << "\"0b0\"";
            }
            return;
        }
        else if (function_name == "oct") {
            if (call_expr->arguments.size() > 0) {
                output << lambdaCapture() << "() { std::stringstream ss; ss << std::oct << ";
                generateExpression(call_expr->arguments[0].get());
                output << "; return ss.str(); }()";
            }
            else {
                output << "\"0o0\"";
            }
            return;
        }
        else if (function_name == "hex") {
            if (call_expr->arguments.size() > 0) {
                output << lambdaCapture() << "() { std::stringstream ss; ss << std::hex << ";
                generateExpression(call_expr->arguments[0].get());
                output << "; return ss.str(); }()";
            }
            else {
                output << "\"0x0\"";
            }
            return;
        }
        else if (function_name == "chr") {
            if (call_expr->arguments.size() > 0) {
                output << "static_cast<char>(";
                generateExpression(call_expr->arguments[0].get());
                output << ")";
            }
            else {
                output << "'\\0'";
            }
            return;
        }
        else if (function_name == "ord") {
            if (call_expr->arguments.size() > 0) {
                output << "static_cast<int>(";
                generateExpression(call_expr->arguments[0].get());
                output << ")";
            }
            else {
                output << "0";
            }
            return;
        }
        else if (function_name == "range") {
            if (call_expr->arguments.size() == 1) {
                output << lambdaCapture() << "() { std::vector<int> result; for(int i = 0; i < ";
                generateExpression(call_expr->arguments[0].get());
                output << "; i++) result.push_back(i); return result; }()";
            }
            else if (call_expr->arguments.size() == 2) {
                output << lambdaCapture() << "() { std::vector<int> result; for(int i = ";
                generateExpression(call_expr->arguments[0].get());
                output << "; i < ";
                generateExpression(call_expr->arguments[1].get());
                output << "; i++) result.push_back(i); return result; }()";
            }
            else if (call_expr->arguments.size() == 3) {
                output << lambdaCapture() << "() { std::vector<int> result; for(int i = ";
                generateExpression(call_expr->arguments[0].get());
                output << "; i < ";
                generateExpression(call_expr->arguments[1].get());
                output << "; i += ";
                generateExpression(call_expr->arguments[2].get());
                output << ") result.push_back(i); return result; }()";
            }
            else {
                output << "std::vector<int>()";
            }
            return;
        }
        else if (function_name == "sorted") {
            if (call_expr->arguments.size() > 0) {
                output << lambdaCapture() << "() { auto tmp = ";
                generateExpression(call_expr->arguments[0].get());
                output << "; std::sort(tmp.begin(), tmp.end()); return tmp; }()";
            }
            else {
                output << "std::vector<int>()";
            }
            return;
        }
        else if (function_name == "reversed") {
            if (call_expr->arguments.size() > 0) {
                output << lambdaCapture() << "() { auto tmp = ";
                generateExpression(call_expr->arguments[0].get());
                output << "; std::reverse(tmp.begin(), tmp.end()); return tmp; }()";
            }
            else {
                output << "std::vector<int>()";
            }
            return;
        }
        else if (function_name == "enumerate") {
            if (call_expr->arguments.size() > 0) {
                output << lambdaCapture() << "() { auto&& container =  ";
                generateExpression(call_expr->arguments[0].get());
                output << "; std::vector<std::pair<int, std::decay_t<decltype(*container.begin())>>> result; ";
                output << "int index = 0; for(auto& elem : container) { result.push_back({index++, elem}); } return result; }()";
            }
            else {
                output << "std::vector<std::pair<int, int>>()";
            }
            return;
        }
        else if (function_name == "zip" && call_expr->arguments.size() == 2) {
            output << lambdaCapture() << "() { auto&& pycpp_a = ";
            generateExpression(call_expr->arguments[0].get());
            output << "; auto&& pycpp_b = ";
            generateExpression(call_expr->arguments[1].get());
            output << "; std::vector<std::pair<std::decay_t<decltype(*pycpp_a.begin())>, "
                      "std::decay_t<decltype(*pycpp_b.begin())>>> pycpp_r; "
                      "auto pycpp_ia = pycpp_a.begin(); auto pycpp_ib = pycpp_b.begin(); "
                      "for (; pycpp_ia != pycpp_a.end() && pycpp_ib != pycpp_b.end(); ++pycpp_ia, ++pycpp_ib) "
                      "pycpp_r.push_back({*pycpp_ia, *pycpp_ib}); return pycpp_r; }()";
            return;
        }
        else if (function_name == "zip") {
            if (call_expr->arguments.size() > 0) {
                output << lambdaCapture() << "() { ";
                // Simplified zip implementation
                output << "/* zip() takes exactly two arguments */ ";
                output << "return std::vector<std::tuple<>>(); }()";
            }
            else {
                output << "std::vector<std::tuple<>>()";
            }
            return;
        }
        else if (function_name == "map") {
            if (call_expr->arguments.size() >= 2) {
                output << lambdaCapture() << "() { auto&& container =  ";
                generateExpression(call_expr->arguments[1].get());
                output << "; std::vector<decltype(";
                generateExpression(call_expr->arguments[0].get());
                output << "(*container.begin()))> result; ";
                output << "for(auto& elem : container) { result.push_back(";
                generateExpression(call_expr->arguments[0].get());
                output << "(elem)); } return result; }()";
            }
            else {
                output << "std::vector<int>()";
            }
            return;
        }
        else if (function_name == "filter") {
            if (call_expr->arguments.size() >= 2) {
                output << lambdaCapture() << "() { auto&& container =  ";
                generateExpression(call_expr->arguments[1].get());
                output << "; std::vector<std::decay_t<decltype(*container.begin())>> result; ";
                output << "for(auto& elem : container) { if(";
                generateExpression(call_expr->arguments[0].get());
                output << "(elem)) result.push_back(elem); } return result; }()";
            }
            else {
                output << "std::vector<int>()";
            }
            return;
        }
        else if (function_name == "divmod") {
            if (call_expr->arguments.size() >= 2) {
                output << "std::make_pair(";
                generateExpression(call_expr->arguments[0].get());
                output << " / ";
                generateExpression(call_expr->arguments[1].get());
                output << ", ";
                generateExpression(call_expr->arguments[0].get());
                output << " % ";
                generateExpression(call_expr->arguments[1].get());
                output << ")";
            }
            else {
                output << "std::make_pair(0, 0)";
            }
            return;
        }
        else if (function_name == "randint" && call_expr->arguments.size() >= 2) {
            useRuntime();   // inclusive on both ends, like Python
            output << "pycpp::randint(";
            generateExpression(call_expr->arguments[0].get());
            output << ", ";
            generateExpression(call_expr->arguments[1].get());
            output << ")";
            return;
        }
        else if (function_name == "choice" && call_expr->arguments.size() == 1) {
            useRuntime();
            output << lambdaCapture() << "() { auto&& pycpp_c = ";
            generateExpression(call_expr->arguments[0].get());
            output << "; return pycpp_c[pycpp::randint(0, static_cast<int>(pycpp_c.size()) - 1)]; }()";
            return;
        }
        else if (function_name == "randint") {
            if (call_expr->arguments.size() >= 2) {
                output << lambdaCapture() << "() { return rand() % (";
                generateExpression(call_expr->arguments[1].get());
                output << " - ";
                generateExpression(call_expr->arguments[0].get());
                output << ") + ";
                generateExpression(call_expr->arguments[0].get());
                output << "; }()";
            }
            else {
                output << "0";
            }
            return;
        }
        else if (function_name == "choice") {
            if (call_expr->arguments.size() > 0) {
                output << lambdaCapture() << "() { auto&& container =  ";
                generateExpression(call_expr->arguments[0].get());
                output << "; return container[rand() % container.size()]; }()";
            }
            else {
                output << "0";
            }
            return;
        }
        else if (function_name == "shuffle") {
            if (call_expr->arguments.size() > 0) {
                output << lambdaCapture() << "() { auto tmp = ";
                generateExpression(call_expr->arguments[0].get());
                output << "; std::shuffle(tmp.begin(), tmp.end(), std::mt19937(std::random_device{}())); return tmp; }()";
            }
            else {
                output << "std::vector<int>()";
            }
            return;
        }
        else if (function_name == "mem.alloc" || function_name == "malloc") {
            output << "static_cast<int*>(malloc(";
            if (call_expr->arguments.size() > 0) {
                generateExpression(call_expr->arguments[0].get());
                output << " * sizeof(int)";
            }
            output << "))";
            return;
        }
        else if (function_name == "mem.free" || function_name == "free") {
            output << "free(";
            if (call_expr->arguments.size() > 0) {
                generateExpression(call_expr->arguments[0].get());
            }
            output << ")";
            return;
        }

        // Handle member function calls (non-built-in)
        if (auto member_access = dynamic_cast<MemberAccess*>(call_expr->callee.get())) {
            std::string method = member_access->member;
            bool arrow = isPointerExpr(member_access->object.get());   // [fix.py] handles chains too
            Type* object_type = nullptr;
            if (auto ident = dynamic_cast<Identifier*>(member_access->object.get())) {
                if (ident->name == "this") arrow = true;
                object_type = lookupVar(ident->name);
            }
            // list.append(x) -> push_back(x). A variable declared as a user class keeps its own method.
            if (method == "append" && call_expr->arguments.size() == 1 && !dynamic_cast<BasicType*>(object_type)) {
                method = "push_back";
            }

            generateObjectOperand(member_access->object.get());
            output << (arrow ? "->" : ".") << method << "(";
            for (size_t i = 0; i < call_expr->arguments.size(); ++i) {
                if (i > 0) output << ", ";
                generateExpression(call_expr->arguments[i].get());
            }
            output << ")";
            return;
        }

        // Regular function call
        generateExpression(call_expr->callee.get());
        output << "(";
        for (size_t i = 0; i < call_expr->arguments.size(); ++i) {
            generateExpression(call_expr->arguments[i].get());
            if (i < call_expr->arguments.size() - 1) {
                output << ", ";
            }
        }
        output << ")";
        return;
    }

    // Handle UnaryOp
    if (auto unary_op = dynamic_cast<UnaryOp*>(expr)) {
        visit(*unary_op);
        return;
    }

    // Handle TernaryOp
    if (auto ternary_op = dynamic_cast<TernaryOp*>(expr)) {
        visit(*ternary_op);
        return;
    }

    // Handle NoneLiteral
    if (auto none_lit = dynamic_cast<NoneLiteral*>(expr)) {
        visit(*none_lit);
        return;
    }

    // Enhanced BinaryOp handling for Pythonic operators
    if (auto bin_op = dynamic_cast<BinaryOp*>(expr)) {
        // Handle "is not" operator specially for nullptr checks
        if (bin_op->op == "is not") {
            output << "(";
            generateExpression(bin_op->left.get());
            output << " != nullptr)";
            return;
        }
        // Handle "is" operator for nullptr checks
        else if (bin_op->op == "is") {
            if (auto right_ident = dynamic_cast<Identifier*>(bin_op->right.get())) {
                if (right_ident->name == "None") {
                    output << "(";
                    generateExpression(bin_op->left.get());
                    output << " == nullptr)";
                    return;
                }
            }
        }

        if (bin_op->op == "in" || bin_op->op == "not in") {
            useRuntime();
            output << (bin_op->op == "in" ? "pycpp::contains(" : "!pycpp::contains(");
            if (dynamic_cast<ListLiteral*>(bin_op->right.get())) output << "std::vector";
            generateExpression(bin_op->right.get());
            output << ", ";
            generateExpression(bin_op->left.get());
            output << ")";
            return;
        }

        std::unordered_map<std::string, std::string> operator_map = {
            {"and", "&&"},
            {"or", "||"},
            {"not in", "!="}, // Simplified
            {"in", "=="},     // Simplified
        };

        auto it = operator_map.find(bin_op->op);
        if (it != operator_map.end()) {
            output << "(";
            generateExpression(bin_op->left.get());
            output << " " << it->second << " ";
            generateExpression(bin_op->right.get());
            output << ")";
            return;
        }

        if (bin_op->op == "/" || bin_op->op == "//" || bin_op->op == "%" || bin_op->op == "**") {
            useRuntime();
            output << (bin_op->op == "/"  ? "pycpp::truediv("
                     : bin_op->op == "//" ? "pycpp::floordiv("
                     : bin_op->op == "%"  ? "pycpp::mod(" : "pycpp::power(");
            generateExpression(bin_op->left.get());
            output << ", ";
            generateExpression(bin_op->right.get());
            output << ")";
            return;
        }

        // Handle power operator
        if (bin_op->op == "**") {
            output << "std::pow(";
            generateExpression(bin_op->left.get());
            output << ", ";
            generateExpression(bin_op->right.get());
            output << ")";
            return;
        }

        // Handle floor division
        if (bin_op->op == "//") {
            output << "static_cast<int>(";
            generateExpression(bin_op->left.get());
            output << " / ";
            generateExpression(bin_op->right.get());
            output << ")";
            return;
        }

        if (bin_op->op == "//=" || bin_op->op == "%=" || bin_op->op == "**=") {
            // `x //= y` must not be emitted as-is: in C++ that starts a comment
            useRuntime();
            generateExpression(bin_op->left.get());
            output << " = "
                   << (bin_op->op == "//=" ? "pycpp::floordiv("
                       : bin_op->op == "%=" ? "pycpp::mod(" : "pycpp::power(");
            generateExpression(bin_op->left.get());
            output << ", ";
            generateExpression(bin_op->right.get());
            output << ")";
            return;
        }

        // Handle compound assignments
        if (bin_op->op == "+=" || bin_op->op == "-=" || bin_op->op == "*=" ||
            bin_op->op == "/=" || bin_op->op == "%=" || bin_op->op == "&=" ||
            bin_op->op == "|=" || bin_op->op == "^=" || bin_op->op == "<<=" ||
            bin_op->op == ">>=" || bin_op->op == "**=" || bin_op->op == "//=") {

            generateExpression(bin_op->left.get());
            output << " " << bin_op->op << " ";
            generateExpression(bin_op->right.get());
            return;
        }

        // Enhanced operator handling with special case for member assignments
        if (bin_op->op == "-=" || bin_op->op == "+=" || bin_op->op == "*=" || bin_op->op == "/=") {
            // Generate compound assignment directly without parentheses
            generateExpression(bin_op->left.get());
            output << " " << bin_op->op << " ";
            generateExpression(bin_op->right.get());
        }
        else if (bin_op->op == "=") {
            // Special handling for member assignments in constructors
            if (auto member_access = dynamic_cast<MemberAccess*>(bin_op->left.get())) {
                if (auto ident = dynamic_cast<Identifier*>(member_access->object.get())) {
                    if (ident->name == "self" && current_class) {
                        // Convert self.member = value to this->member = value
                        // This handles the case where we have self.name = name in constructors
                        output << "this->" << member_access->member << " = ";
                        generateExpression(bin_op->right.get());
                        return; // Early return to avoid extra parentheses
                    }
                    // [fix.py] Any other object: the member-access generator picks '.' or '->'
                    generateExpression(member_access);
                    output << " = ";
                    generateExpression(bin_op->right.get());
                    return;
                }
            }

            // Also handle the case where we have direct member variable declarations
            // that get parsed as assignments in the constructor body
            if (auto ident = dynamic_cast<Identifier*>(bin_op->left.get())) {
                // Check if this identifier shadows a member variable
                if (current_class && current_function &&
                    (current_function->name == "__init__" || current_function->name.find("__init__") != std::string::npos)) {
                    // In constructor context, check if this might be a member assignment
                    // This is a heuristic - we assume parameter names might shadow member names
                    output << "this->" << ident->name << " = ";
                    generateExpression(bin_op->right.get());
                    return; // Early return
                }
            }

            // Regular assignment
            bool needs_parens = !dynamic_cast<Identifier*>(bin_op->left.get()) &&
                !dynamic_cast<MemberAccess*>(bin_op->left.get()) &&
                !dynamic_cast<CallExpr*>(bin_op->left.get()) &&
                !(dynamic_cast<UnaryOp*>(bin_op->left.get()) &&
                  static_cast<UnaryOp*>(bin_op->left.get())->op == "*");

            if (needs_parens) output << "(";
            generateExpression(bin_op->left.get());
            if (needs_parens) output << ")";

            output << " " << bin_op->op << " ";
            generateExpression(bin_op->right.get());
        }
        else {
            // Regular binary operation - wrap in parentheses for safety
            output << "(";
            generateExpression(bin_op->left.get());
            output << " " << bin_op->op << " ";
            generateExpression(bin_op->right.get());
            output << ")";
        }
        return;
    }

    if (auto ident = dynamic_cast<Identifier*>(expr)) {
        // Handle 'self' in class context
        if (ident->name == "self" && current_class) {
            output << "this";
        }
        else {
            output << ident->name;
        }
    }
    else if (auto int_lit = dynamic_cast<IntLiteral*>(expr)) {
        output << int_lit->value;
    }
    else if (auto float_lit = dynamic_cast<FloatLiteral*>(expr)) {
        output << formatDouble(float_lit->value);
    }
    else if (auto str_lit = dynamic_cast<StringLiteral*>(expr)) {
        output << "\"" << cppEscape(str_lit->value) << "\"";
    }
    else if (auto bool_lit = dynamic_cast<BoolLiteral*>(expr)) {
        output << (bool_lit->value ? "true" : "false");
    }
    // Enhanced member access handling for both objects and modules
    else if (auto member_access = dynamic_cast<MemberAccess*>(expr)) {
        // Check if this is module access (like greetings.hello)
        if (auto ident = dynamic_cast<Identifier*>(member_access->object.get())) {
            // Only identifiers that name an imported module are module accesses;
            // anything else (e.g. a local variable 'player') is an object access
            if (ident->name != "self" && ident->name != "this" &&
                imported_modules.count(ident->name) > 0 &&
                lookupVar(ident->name) == nullptr) {
                // This is likely a module access like greetings.hello
                // For now, generate as direct function call since we include the header
                // In a more advanced implementation, we might use namespaces
                output << member_access->member;
                return; // Early return to avoid the dot operator
            }
        }

        // Original handling for object member access
        if (auto ident = dynamic_cast<Identifier*>(member_access->object.get())) {
            if (ident->name == "self" && current_class) {
                // In class context, convert self.member to this->member
                output << "this->" << member_access->member;
            }
            else if (ident->name == "this") {
                // Handle 'this' directly - use -> for member access
                output << "this->" << member_access->member;
            }
            else if (isPointerExpr(ident)) {
                // Pointer variable: obj->member
                generateExpression(member_access->object.get());
                output << "->" << member_access->member;
            }
            else {
                // For regular objects, use dot operator
                generateExpression(member_access->object.get());
                output << "." << member_access->member;
            }
        }
        else {
            // [fix.py] Chained / complex objects: '->' whenever the object is itself a pointer
            generateObjectOperand(member_access->object.get());
            output << (isPointerExpr(member_access->object.get()) ? "->" : ".")
                   << member_access->member;
        }
    }
    else if (auto list_lit = dynamic_cast<ListLiteral*>(expr)) {
        output << "{";
        for (size_t i = 0; i < list_lit->elements.size(); ++i) {
            generateExpression(list_lit->elements[i].get());
            if (i < list_lit->elements.size() - 1) {
                output << ", ";
            }
        }
        output << "}";
    }
    else if (auto dict_lit = dynamic_cast<DictLiteral*>(expr)) {
        output << "{";
        for (size_t i = 0; i < dict_lit->items.size(); ++i) {
            output << "{";
            generateExpression(dict_lit->items[i].first.get());
            output << ", ";
            generateExpression(dict_lit->items[i].second.get());
            output << "}";
            if (i < dict_lit->items.size() - 1) {
                output << ", ";
            }
        }
        output << "}";
    }
    else {
        output << "/* expression */";
    }
}

// ===== Visitor Pattern Implementations =====

void CodeGenerator::visit(Program& node) {
    for (auto& stmt : node.statements) {
        stmt->accept(*this);
    }
}

void CodeGenerator::visit(FunctionDef& node) {
    generateFunction(&node);
}

void CodeGenerator::visit(ClassDef& node) {
    generateClass(&node);
}

void CodeGenerator::visit(StructDef& node) {
    output << getIndent() << "struct " << node.name << " {\n";
    indent();
    for (auto& field : node.fields) {
        output << getIndent();
        generateType(field->type.get());
        output << " " << field->var_name;
        if (field->value) {
            output << " = ";
            generateExpression(field->value.get());
        }
        output << ";\n";
    }
    dedent();
    output << getIndent() << "};\n";
}

void CodeGenerator::visit(TemplateDef& node) {
    output << getIndent() << "template<typename " << node.type_param << ">\n";
    if (node.templated_class) {
        node.templated_class->accept(*this);
    }
    if (node.templated_function) {
        node.templated_function->accept(*this);
    }
}

void CodeGenerator::visit(ConstexprDef& node) {
    output << getIndent() << "constexpr ";
    generateType(node.type.get());
    output << " " << node.name << " = ";
    if (node.value) {
        generateExpression(node.value.get());
    }
    output << ";\n";
}

void CodeGenerator::visit(VarDecl& node) {
    generateVarDecl(&node);
    // Remember the declared type so later member accesses pick '.' or '->' correctly.
    // 'self.x' declarations are members, not locals.
    if (node.var_name.find("self.") != 0 && node.type) {
        declareVar(node.var_name, node.type.get());
    }
}

void CodeGenerator::visit(ReturnStmt& node) {
    output << getIndent() << "return";
    if (node.value) {
        output << " ";
        generateExpression(node.value.get());
    }
    output << ";\n";
}

void CodeGenerator::visit(PassStmt& node) {
    output << getIndent() << (node.is_ellipsis ? "// ...\n" : "// pass\n");
}

void CodeGenerator::visit(BreakStmt& node) {
    output << getIndent() << "break;\n";
}

void CodeGenerator::visit(ContinueStmt& node) {
    output << getIndent() << "continue;\n";
}

void CodeGenerator::visit(IfStmt& node) {
    output << getIndent() << "if (";
    if (node.condition) {
        generateExpression(node.condition.get());
    }
    output << ") {\n";
    indent();
    for (auto& stmt : node.then_branch) {
        generateStatement(stmt.get());
    }
    dedent();
    output << getIndent() << "}\n";

    // Handle elif branches
    for (auto& elif_pair : node.elif_branches) {
        output << getIndent() << "else if (";
        if (elif_pair.first) {
            generateExpression(elif_pair.first.get());
        }
        output << ") {\n";
        indent();
        for (auto& stmt : elif_pair.second) {
            generateStatement(stmt.get());
        }
        dedent();
        output << getIndent() << "}\n";
    }

    // FIX: Only generate else branch if this if statement actually has one
    // Don't generate else for nested if statements that don't have their own else
    if (!node.else_branch.empty()) {
        output << getIndent() << "else {\n";
        indent();
        for (auto& stmt : node.else_branch) {
            generateStatement(stmt.get());
        }
        dedent();
        output << getIndent() << "}\n";
    }
}

void CodeGenerator::visit(WhileStmt& node) {
    output << getIndent() << "while (";
    if (node.condition) {
        generateExpression(node.condition.get());
    }
    output << ") {\n";
    indent();
    for (auto& stmt : node.body) {
        generateStatement(stmt.get());
    }
    dedent();
    output << getIndent() << "}\n";
}

// for i in range(stop) / range(start, stop) / range(start, stop, step) becomes a plain
// C++ counting loop instead of materialising a std::vector first. As in Python, stop
// and step are evaluated once, before the loop starts.
void CodeGenerator::generateRangeLoop(ForStmt& node, CallExpr* call) {
    auto& args = call->arguments;
    const std::string var = node.var_name;

    auto literalInt = [](Expr* e, long long& out) {
        if (auto lit = dynamic_cast<IntLiteral*>(e)) {
            out = lit->value;
            return true;
        }
        if (auto un = dynamic_cast<UnaryOp*>(e)) {
            if (un->op == "-") {
                if (auto lit = dynamic_cast<IntLiteral*>(un->operand.get())) {
                    out = -static_cast<long long>(lit->value);
                    return true;
                }
            }
        }
        return false;
    };

    Expr* start_expr = args.size() >= 2 ? args[0].get() : nullptr;
    Expr* stop_expr = args.size() >= 2 ? args[1].get() : args[0].get();
    Expr* step_expr = args.size() == 3 ? args[2].get() : nullptr;

    const std::string start = start_expr ? exprToString(start_expr) : "0";
    const std::string stop = exprToString(stop_expr);

    long long ignored = 0;
    const bool stop_is_literal = literalInt(stop_expr, ignored);
    const std::string end_name = "pycpp_end_" + var;
    const std::string step_name = "pycpp_step_" + var;

    // A non-literal stop is hoisted so it is evaluated once, as in Python
    std::string init = "int " + var + " = " + start;
    if (!stop_is_literal) init += ", " + end_name + " = " + stop;
    const std::string bound = stop_is_literal ? stop : end_name;

    std::string cond, incr;
    long long step = 1;
    if (!step_expr || literalInt(step_expr, step)) {
        if (step == 0) throw std::runtime_error("range() step must not be zero");
        if (step > 0) {
            cond = var + " < " + bound;
            incr = (step == 1) ? "++" + var : var + " += " + std::to_string(step);
        }
        else {
            cond = var + " > " + bound;
            incr = (step == -1) ? "--" + var : var + " += " + std::to_string(step);
        }
    }
    else {
        // Step only known at run time: the direction is decided by its sign
        init += ", " + step_name + " = " + exprToString(step_expr);
        cond = "(" + step_name + " > 0 ? " + var + " < " + bound + " : " + var + " > " + bound + ")";
        incr = var + " += " + step_name;
    }

    output << getIndent() << "for (" << init << "; " << cond << "; " << incr << ") {\n";
    indent();
    pushScope();
    declareVar(var, nullptr);
    for (auto& stmt : node.body) {
        generateStatement(stmt.get());
    }
    popScope();
    dedent();
    output << getIndent() << "}\n";
}

void CodeGenerator::visit(ForStmt& node) {
    // for i in range(...): a counting loop, no temporary vector
    if (node.var_names.size() <= 1) {
        if (auto call = dynamic_cast<CallExpr*>(node.iterable.get())) {
            auto callee = dynamic_cast<Identifier*>(call->callee.get());
            const size_t argc = call->arguments.size();
            if (callee && callee->name == "range" && !call->is_subscript &&
                call->type_args.empty() && !lookupVar("range") && argc >= 1 && argc <= 3) {
                generateRangeLoop(node, call);
                return;
            }
        }
    }

    // Preserve the iterable's element type in the loop-variable scope. In particular,
    // list[*T] must bind ptr as *T so member access emits ptr->field, not ptr.field.
    Type* loop_element_type = nullptr;
    if (node.var_names.size() <= 1 && node.iterable) {
        TypeInfo iterable_info = exprTypeInfo(node.iterable.get());
        if (iterable_info.known && iterable_info.elem) {
            loop_element_type = iterable_info.elem;
        }
    }

    pushScope();
    for (const auto& name : node.var_names) {
        declareVar(name, node.var_names.size() <= 1 ? loop_element_type : nullptr);
    }
    if (node.var_names.empty()) declareVar(node.var_name, loop_element_type);

    if (node.var_names.size() > 1) {
        // for k, v in d.items(): -> for (auto& [k, v] : d)
        output << getIndent() << "for (auto& [";
        for (size_t i = 0; i < node.var_names.size(); ++i) {
            if (i > 0) output << ", ";
            output << node.var_names[i];
        }
        output << "] : ";

        Expr* iterable = node.iterable.get();
        if (auto call = dynamic_cast<CallExpr*>(iterable)) {
            if (auto member = dynamic_cast<MemberAccess*>(call->callee.get())) {
                if (member->member == "items" && call->arguments.empty()) {
                    iterable = member->object.get();
                }
            }
        }
        generateExpression(iterable);
    }
    else {
        output << getIndent() << "for (auto& " << node.var_name << " : ";
        if (node.iterable) {
            generateExpression(node.iterable.get());
        }
    }
    output << ") {\n";
    indent();
    for (auto& stmt : node.body) {
        generateStatement(stmt.get());
    }
    dedent();
    output << getIndent() << "}\n";
    popScope();
}
// `unsafe:` only changes what PyCPP's checks allow; it has no C++ counterpart, so its
// statements are emitted in place.
void CodeGenerator::visit(UnsafeBlock& node) {
    for (auto& stmt : node.body) {
        generateStatement(stmt.get());
    }
}

void CodeGenerator::visit(ExprStmt& node) {
    output << getIndent();
    if (node.expr) {
        generateExpression(node.expr.get());
    }
    output << ";\n";
}

void CodeGenerator::visit(Block& node) {
    output << getIndent() << "{\n";
    indent();
    for (auto& stmt : node.statements) {
        generateStatement(stmt.get());
    }
    dedent();
    output << getIndent() << "}\n";
}

void CodeGenerator::visit(IntLiteral& node) {
    output << node.value;
}

void CodeGenerator::visit(FloatLiteral& node) {
    output << formatDouble(node.value);
}

void CodeGenerator::visit(StringLiteral& node) {
    output << "\"" << cppEscape(node.value) << "\"";
}

void CodeGenerator::visit(BoolLiteral& node) {
    output << (node.value ? "true" : "false");
}

void CodeGenerator::visit(NoneLiteral& node) {
    output << "nullptr";
}

void CodeGenerator::visit(Identifier& node) {
    output << node.name;
}

void CodeGenerator::visit(BinaryOp& node) {
    generateExpression(&node);
}

void CodeGenerator::visit(UnaryOp& node) {
    if (node.op == "not") {
        output << "!";
        generateExpression(node.operand.get());
    }
    else {
        output << node.op;
        if (dynamic_cast<UnaryOp*>(node.operand.get())) output << ' ';
        generateExpression(node.operand.get());
    }
}

void CodeGenerator::visit(TernaryOp& node) {
    output << "(";
    generateExpression(node.condition.get());
    output << " ? ";
    generateExpression(node.then_expr.get());
    output << " : ";
    generateExpression(node.else_expr.get());
    output << ")";
}

void CodeGenerator::visit(CallExpr& node) {
    generateExpression(&node);
}

void CodeGenerator::visit(ListLiteral& node) {
    generateExpression(&node);
}

void CodeGenerator::visit(DictLiteral& node) {
    generateExpression(&node);
}

void CodeGenerator::visit(MemberAccess& node) {
    generateExpression(&node);
}

void CodeGenerator::visit(BasicType& node) {
    if (node.name == "str") {
        output << "std::string";
    }
    else if (node.name == "std::string") {
        output << "std::string";
    }
    else {
        output << node.name;
    }
}

void CodeGenerator::visit(PointerType& node) {
    generateType(&node);
}

void CodeGenerator::visit(ListType& node) {
    generateType(&node);
}

void CodeGenerator::visit(DictType& node) {
    generateType(&node);
}

void CodeGenerator::visit(ImportStmt& node) {
    output << getIndent() << "// import " << node.module;
    if (!node.imports.empty()) {
        output << " (importing: ";
        for (size_t i = 0; i < node.imports.size(); ++i) {
            if (i > 0) output << ", ";
            output << node.imports[i];
        }
        output << ")";
    }
    output << "\n";

    // The actual include will be generated in generateProgram
}

void CodeGenerator::visit(UseStmt& node) {
    output << getIndent() << "// use " << node.library << "\n";
}

// ===== defer =====
// `defer stmt` becomes an RAII object, so it runs on normal exit, early return,
// break and continue, in reverse order of declaration.
void CodeGenerator::visit(DeferStmt& node) {
    useRuntime();
    output << getIndent() << "pycpp::Defer pycpp_defer_" << ++defer_counter << "([&]() {\n";
    indent(); pushScope();
    for (auto& s : node.body) generateStatement(s.get());
    popScope(); dedent();
    output << getIndent() << "});\n";
}

// ===== try / except / else / finally / raise =====
// try/except become C++ try/catch. `finally` is a pycpp::Defer object declared first, so
// it is destroyed last: it runs after the handlers, on return/break/continue, and while
// an exception propagates. `else` runs when the try body finished without throwing.
void CodeGenerator::visit(TryStmt& node) {
    useRuntime();
    const int id = ++try_counter;
    const std::string ok = "pycpp_try_ok_" + std::to_string(id);
    const bool has_else = !node.else_body.empty();

    auto emitBody = [&](const std::vector<std::unique_ptr<Stmt>>& body, const std::string& tail = "") {
        indent(); pushScope();
        for (auto& s : body) generateStatement(s.get());
        if (!tail.empty()) output << getIndent() << tail << "\n";
        popScope(); dedent();
    };

    output << getIndent() << "{\n";
    indent();

    if (!node.finally_body.empty()) {
        output << getIndent() << "pycpp::Defer pycpp_finally_" << id << "([&]() {\n";
        emitBody(node.finally_body);
        output << getIndent() << "});\n";
    }
    if (has_else) output << getIndent() << "bool " << ok << " = false;\n";

    // `try { }` with no catch is not valid C++ (a try/finally has no handlers)
    output << getIndent() << (node.handlers.empty() ? "{\n" : "try {\n");
    emitBody(node.body, has_else ? ok + " = true;" : "");
    output << getIndent() << "}\n";

    for (auto& h : node.handlers) {
        std::vector<std::string> types = h.type_names;
        if (types.empty()) types.push_back("");          // `except:` -> catch (...)
        for (const auto& type : types) {                 // except (A, B): one catch per type
            output << getIndent() << "catch (";
            if (type.empty()) {
                output << "...";
            }
            else {
                output << "const " << type << "&";
                if (!h.var_name.empty()) output << " " << h.var_name;
            }
            output << ") {\n";
            indent(); pushScope();
            if (!h.var_name.empty()) declareVar(h.var_name, nullptr);
            handler_vars.push_back(h.var_name);
            for (auto& s : h.body) generateStatement(s.get());
            handler_vars.pop_back();
            popScope(); dedent();
            output << getIndent() << "}\n";
        }
    }

    if (has_else) {
        output << getIndent() << "if (" << ok << ") {\n";
        emitBody(node.else_body);
        output << getIndent() << "}\n";
    }

    dedent();
    output << getIndent() << "}\n";
}

void CodeGenerator::visit(RaiseStmt& node) {
    useRuntime();
    output << getIndent();
    if (!node.value) {
        output << "throw;\n";                             // re-raise the exception being handled
        return;
    }
    if (auto id = dynamic_cast<Identifier*>(node.value.get())) {
        bool is_handler_var = false;
        for (const auto& v : handler_vars) if (!v.empty() && v == id->name) is_handler_var = true;
        if (is_handler_var) output << "throw;\n";         // raise e
        else output << "throw " << id->name << "();\n";   // raise MyError
        return;
    }
    output << "throw ";
    generateExpression(node.value.get());
    output << ";\n";
}

// ===== match / case =====
// A real `switch` when every pattern is an integer literal and no case body uses
// `break` (inside a switch it would mean something else); an if/else-if chain for
// everything else (strings, floats, ...).
static bool isIntPattern(Expr* e) {
    if (dynamic_cast<IntLiteral*>(e)) return true;
    auto un = dynamic_cast<UnaryOp*>(e);
    return un && un->op == "-" && dynamic_cast<IntLiteral*>(un->operand.get());
}

static bool containsBreak(const std::vector<std::unique_ptr<Stmt>>& body) {   // does not enter loops
    for (auto& s : body) {
        if (dynamic_cast<BreakStmt*>(s.get())) return true;
        if (auto i = dynamic_cast<IfStmt*>(s.get())) {
            if (containsBreak(i->then_branch) || containsBreak(i->else_branch)) return true;
            for (auto& e : i->elif_branches) if (containsBreak(e.second)) return true;
        }
        else if (auto u = dynamic_cast<UnsafeBlock*>(s.get())) { if (containsBreak(u->body)) return true; }
        else if (auto b = dynamic_cast<Block*>(s.get())) { if (containsBreak(b->statements)) return true; }
        else if (auto m = dynamic_cast<MatchStmt*>(s.get())) {
            for (auto& c : m->cases) if (containsBreak(c.body)) return true;
        }
    }
    return false;
}

void CodeGenerator::visit(MatchStmt& node) {
    bool use_switch = true;
    for (auto& c : node.cases) {
        if (containsBreak(c.body)) use_switch = false;
        for (auto& p : c.patterns) if (!isIntPattern(p.get())) use_switch = false;
    }

    auto emitBody = [&](const std::vector<std::unique_ptr<Stmt>>& body) {
        indent(); pushScope();
        for (auto& s : body) generateStatement(s.get());
        popScope(); dedent();
    };

    if (use_switch) {
        output << getIndent() << "switch (";
        generateExpression(node.subject.get());
        output << ") {\n";
        for (auto& c : node.cases) {
            if (c.is_default) {
                output << getIndent() << "default: {\n";
            }
            else {
                for (size_t i = 0; i < c.patterns.size(); ++i) {
                    output << getIndent() << "case ";
                    generateExpression(c.patterns[i].get());
                    output << (i + 1 < c.patterns.size() ? ":\n" : ": {\n");
                }
            }
            emitBody(c.body);
            const bool jumps = !c.body.empty() &&
                (dynamic_cast<ReturnStmt*>(c.body.back().get()) || dynamic_cast<ContinueStmt*>(c.body.back().get()));
            if (!jumps) { indent(); output << getIndent() << "break;\n"; dedent(); }
            output << getIndent() << "}\n";
        }
        output << getIndent() << "}\n";
        return;
    }

    // if-chain: the subject is evaluated once
    const std::string tmp = "pycpp_match_" + std::to_string(++match_counter);
    output << getIndent() << "{\n";
    indent();
    output << getIndent() << "const auto& " << tmp << " = ";
    generateExpression(node.subject.get());
    output << ";\n";
    bool first = true;
    const MatchCase* fallback = nullptr;
    for (auto& c : node.cases) {
        if (c.is_default) { fallback = &c; continue; }
        output << getIndent() << (first ? "if (" : "else if (");
        for (size_t i = 0; i < c.patterns.size(); ++i) {
            if (i > 0) output << " || ";
            output << tmp << " == ";
            generateExpression(c.patterns[i].get());
        }
        output << ") {\n";
        emitBody(c.body);
        output << getIndent() << "}\n";
        first = false;
    }
    if (fallback) {
        output << getIndent() << (first ? "{\n" : "else {\n");
        emitBody(fallback->body);
        output << getIndent() << "}\n";
    }
    dedent();
    output << getIndent() << "}\n";
}
