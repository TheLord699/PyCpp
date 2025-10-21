#include "../include/code_generator.h"
#include "../include/header_generator.h"
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <numeric>

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
};

// CodeGenerator implementation
CodeGenerator::CodeGenerator() : indent_level(0), current_function(nullptr), current_class(nullptr) {}

std::string CodeGenerator::generate(std::unique_ptr<Program>& program, IncludeManager& include_manager) {
    output.str(""); // Clear any existing content
    generateProgram(program.get(), include_manager);
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
    // FIRST: Analyze the program to detect required features
    analyzeProgramForIncludes(program, include_manager);

    // Extract imports to generate includes
    std::vector<std::string> import_includes;
    for (auto& stmt : program->statements) {
        if (auto import_stmt = dynamic_cast<ImportStmt*>(stmt.get())) {
            // Generate include for imported module
            import_includes.push_back("\"" + import_stmt->module + ".h\"");
        }
    }

    // Generate includes from include manager
    auto includes = include_manager.getRequiredIncludes();
    for (const auto& include : includes) {
        // Extract just the filename if it's a local header with full path
        std::string include_stmt = include;
        if (include[0] == '"' && include.find("temp\\") != std::string::npos) {
            // Extract just the filename from paths like "C:\Users\...\temp\test_output.h"
            size_t last_slash = include.find_last_of("\\/");
            size_t end_quote = include.find_last_of('"');
            if (last_slash != std::string::npos && end_quote != std::string::npos) {
                std::string filename = include.substr(last_slash + 1, end_quote - last_slash - 1);
                include_stmt = "\"" + filename + "\"";
            }
        }
        output << "#include " << include_stmt << "\n";
    }

    // Add import includes
    for (const auto& include : import_includes) {
        output << "#include " << include << "\n";
    }

    output << "\n";

    // Generate actual code
    for (auto& stmt : program->statements) {
        stmt->accept(*this);
        output << "\n";
    }
}

void CodeGenerator::generateStatement(Stmt* stmt) {
    stmt->accept(*this);
}

void CodeGenerator::generateFunction(FunctionDef* func) {
    FunctionDef* previous_function = current_function;
    current_function = func;

    output << getIndent();

    // Handle constructors specially
    bool is_constructor = (func->name == "__init__" && current_class);

    if (is_constructor) {
        // For constructors, use the class name instead of __init__
        output << current_class->name;
    }
    else {
        // Return type and name for regular functions
        generateType(func->return_type.get());
        output << " " << func->name;
    }

    output << "(";

    // Parameters - handle self parameter specially
    bool first_param = true;
    bool has_self_param = false;

    for (size_t i = 0; i < func->parameters.size(); ++i) {
        const auto& param = func->parameters[i];

        // Skip 'self' parameter for member functions
        if (param.name == "self" && current_class) {
            has_self_param = true;
            continue;
        }

        if (!first_param) {
            output << ", ";
        }
        first_param = false;

        generateType(param.type.get());
        output << " " << param.name;

        if (param.default_value) {
            output << " = ";
            generateExpression(param.default_value.get());
        }
    }

    output << ")";

    // Handle constructor initialization lists
    if (is_constructor && current_class) {
        generateMemberInitializerList(func, current_class);
    }

    output << " {\n";
    indent();

    // Body - handle constructor body specially to avoid duplicate member assignments
    if (is_constructor) {
        generateConstructorBody(func, current_class);
    }
    else {
        // Regular function body
        for (auto& stmt : func->body) {
            generateStatement(stmt.get());
        }
    }

    // Add implicit return for main function if missing
    if (func->name == "main" && func->return_type->name == "int") {
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

    current_function = previous_function;
}

void CodeGenerator::generateMemberInitializerList(FunctionDef* constructor, ClassDef* cls) {
    bool has_initializers = false;
    std::vector<std::pair<std::string, std::string>> initializers;

    // Collect member variables that need initialization from constructor parameters
    for (auto& stmt : constructor->body) {
        if (auto expr_stmt = dynamic_cast<ExprStmt*>(stmt.get())) {
            if (auto bin_op = dynamic_cast<BinaryOp*>(expr_stmt->expr.get())) {
                if (bin_op->op == "=") {
                    // Check if this is a member assignment: self.member = parameter
                    if (auto member_access = dynamic_cast<MemberAccess*>(bin_op->left.get())) {
                        if (auto self_ident = dynamic_cast<Identifier*>(member_access->object.get())) {
                            if (self_ident->name == "self") {
                                std::string member_name = member_access->member;

                                // Check if right side is a parameter
                                if (auto param_ident = dynamic_cast<Identifier*>(bin_op->right.get())) {
                                    // Verify this parameter exists in constructor
                                    for (const auto& param : constructor->parameters) {
                                        if (param.name == param_ident->name) {
                                            initializers.emplace_back(member_name, param_ident->name);
                                            has_initializers = true;
                                            break;
                                        }
                                    }
                                }
                                // Handle literal assignments in initializer list
                                else if (auto int_lit = dynamic_cast<IntLiteral*>(bin_op->right.get())) {
                                    initializers.emplace_back(member_name, std::to_string(int_lit->value));
                                    has_initializers = true;
                                }
                                else if (auto str_lit = dynamic_cast<StringLiteral*>(bin_op->right.get())) {
                                    initializers.emplace_back(member_name, "\"" + str_lit->value + "\"");
                                    has_initializers = true;
                                }
                                else if (auto float_lit = dynamic_cast<FloatLiteral*>(bin_op->right.get())) {
                                    initializers.emplace_back(member_name, std::to_string(float_lit->value));
                                    has_initializers = true;
                                }
                                else if (auto bool_lit = dynamic_cast<BoolLiteral*>(bin_op->right.get())) {
                                    initializers.emplace_back(member_name, bool_lit->value ? "true" : "false");
                                    has_initializers = true;
                                }
                            }
                        }
                    }
                }
            }
        }
        // Also handle variable declarations in constructor body
        else if (auto var_decl = dynamic_cast<VarDecl*>(stmt.get())) {
            if (var_decl->var_name.find("self.") == 0) {
                std::string member_name = var_decl->var_name.substr(5);
                if (var_decl->value) {
                    // For variable declarations with values, we can initialize directly
                    std::stringstream value_stream;
                    CodeGenerator value_generator;
                    value_generator.generateExpression(var_decl->value.get());
                    initializers.emplace_back(member_name, value_stream.str());
                    has_initializers = true;
                }
            }
        }
    }

    if (has_initializers) {
        output << " : ";
        for (size_t i = 0; i < initializers.size(); ++i) {
            if (i > 0) output << ", ";
            output << initializers[i].first << "(" << initializers[i].second << ")";
        }
    }
}

void CodeGenerator::generateConstructorBody(FunctionDef* constructor, ClassDef* cls) {
    // For constructor body, we need to filter out member assignments that are already
    // handled in the initializer list, and handle any additional logic

    std::unordered_set<std::string> initialized_members;

    // First pass: identify members that are initialized in the initializer list
    for (auto& stmt : constructor->body) {
        if (auto expr_stmt = dynamic_cast<ExprStmt*>(stmt.get())) {
            if (auto bin_op = dynamic_cast<BinaryOp*>(expr_stmt->expr.get())) {
                if (bin_op->op == "=") {
                    if (auto member_access = dynamic_cast<MemberAccess*>(bin_op->left.get())) {
                        if (auto self_ident = dynamic_cast<Identifier*>(member_access->object.get())) {
                            if (self_ident->name == "self") {
                                // Check if this is a simple parameter assignment
                                if (auto param_ident = dynamic_cast<Identifier*>(bin_op->right.get())) {
                                    for (const auto& param : constructor->parameters) {
                                        if (param.name == param_ident->name) {
                                            initialized_members.insert(member_access->member);
                                            break;
                                        }
                                    }
                                }
                                // Check if this is a literal assignment
                                else if (dynamic_cast<IntLiteral*>(bin_op->right.get()) ||
                                    dynamic_cast<StringLiteral*>(bin_op->right.get()) ||
                                    dynamic_cast<FloatLiteral*>(bin_op->right.get()) ||
                                    dynamic_cast<BoolLiteral*>(bin_op->right.get())) {
                                    initialized_members.insert(member_access->member);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Second pass: generate the body, skipping already-initialized members
    for (auto& stmt : constructor->body) {
        bool skip_stmt = false;

        // Check if this statement is a member assignment that's already handled
        if (auto expr_stmt = dynamic_cast<ExprStmt*>(stmt.get())) {
            if (auto bin_op = dynamic_cast<BinaryOp*>(expr_stmt->expr.get())) {
                if (bin_op->op == "=") {
                    if (auto member_access = dynamic_cast<MemberAccess*>(bin_op->left.get())) {
                        if (auto self_ident = dynamic_cast<Identifier*>(member_access->object.get())) {
                            if (self_ident->name == "self") {
                                if (initialized_members.find(member_access->member) != initialized_members.end()) {
                                    skip_stmt = true;
                                }
                            }
                        }
                    }
                }
            }
        }
        // Check if this is a member variable declaration that's already handled
        else if (auto var_decl = dynamic_cast<VarDecl*>(stmt.get())) {
            if (var_decl->var_name.find("self.") == 0) {
                std::string member_name = var_decl->var_name.substr(5);
                if (initialized_members.find(member_name) != initialized_members.end()) {
                    skip_stmt = true;
                }
            }
        }

        if (!skip_stmt) {
            generateStatement(stmt.get());
        }
    }

    // If no statements were generated (all were handled in initializer list),
    // add a comment to avoid empty constructor body
    if (constructor->body.empty()) {
        output << getIndent() << "// constructor body\n";
    }
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

                    // Copy the type
                    if (var_decl->type) {
                        if (auto basic_type = dynamic_cast<BasicType*>(var_decl->type.get())) {
                            auto new_type = std::make_unique<BasicType>();
                            new_type->name = basic_type->name;
                            new_attr->type = std::move(new_type);
                        }
                        else if (auto ptr_type = dynamic_cast<PointerType*>(var_decl->type.get())) {
                            auto new_type = std::make_unique<PointerType>();
                            if (ptr_type->base_type) {
                                if (auto base_basic = dynamic_cast<BasicType*>(ptr_type->base_type.get())) {
                                    auto base_type = std::make_unique<BasicType>();
                                    base_type->name = base_basic->name;
                                    new_type->base_type = std::move(base_type);
                                }
                            }
                            new_attr->type = std::move(new_type);
                        }
                    }

                    // Copy the value if present
                    if (var_decl->value) {
                        // For now, we'll just mark that there's a value without deep copying
                        // In a full implementation, you'd want to clone the expression tree
                        new_attr->value = nullptr; // Simplified
                    }

                    cls->attributes.push_back(std::move(new_attr));
                    existing_members.insert(member_name);
                    std::cout << "Extracted member variable from constructor: " << member_name << std::endl;
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
                                                if (auto param_basic = dynamic_cast<BasicType*>(param.type.get())) {
                                                    auto new_type = std::make_unique<BasicType>();
                                                    new_type->name = param_basic->name;
                                                    new_attr->type = std::move(new_type);
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
                                    std::cout << "Inferred member variable from assignment: " << member_name << std::endl;
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
    std::cout << "Generating class " << cls->name << " with " << cls->attributes.size() << " attributes" << std::endl;

    for (auto& attr : cls->attributes) {
        // Extract member name from "self.member_name" pattern
        std::string member_name = attr->var_name;
        if (member_name.find("self.") == 0) {
            member_name = member_name.substr(5); // Remove "self." prefix
        }

        output << getIndent();
        generateType(attr->type.get());
        output << " " << member_name;

        // ONLY output initializers if explicitly provided in PyCPP source
        // Don't add default initialization - let constructor handle it
        if (attr->value) {
            output << " = ";
            generateExpression(attr->value.get());
        }
        // REMOVED: The else block that was adding default initialization
        output << ";\n";
    }

    // Generate methods
    for (auto& method : cls->methods) {
        generateFunction(method.get());
    }

    dedent();
    output << getIndent() << "};\n";

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

            // Handle type conversions for built-in functions
            if (auto call_expr = dynamic_cast<CallExpr*>(var->value.get())) {
                if (auto ident = dynamic_cast<Identifier*>(call_expr->callee.get())) {
                    std::string func_name = ident->name;

                    // If the function returns a different type than the variable declaration,
                    // we may need explicit conversion
                    if (var->type) {
                        if (auto basic_type = dynamic_cast<BasicType*>(var->type.get())) {
                            // input() returns string, but variable might be int/float/etc.
                            if (func_name == "input") {
                                if (basic_type->name == "int" || basic_type->name == "i32" || basic_type->name == "i64") {
                                    output << "std::stoi(";
                                    generateExpression(var->value.get());
                                    output << ")";
                                }
                                else if (basic_type->name == "float" || basic_type->name == "f32" || basic_type->name == "f64" || basic_type->name == "double") {
                                    output << "std::stof(";
                                    generateExpression(var->value.get());
                                    output << ")";
                                }
                                else if (basic_type->name == "bool") {
                                    output << "(";
                                    generateExpression(var->value.get());
                                    output << " == \"true\" || ";
                                    generateExpression(var->value.get());
                                    output << " == \"1\")";
                                }
                                else {
                                    generateExpression(var->value.get());
                                }
                                output << ";\n";
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
    if (auto basic_type = dynamic_cast<BasicType*>(type)) {
        // Map PyCPP types to C++ types
        if (basic_type->name == "str") {
            output << "std::string";
        }
        else if (basic_type->name == "std::string") {
            output << "std::string";
        }
        else if (basic_type->name == "None" || basic_type->name == "NoneType") {
            output << "void";
        }
        else {
            output << basic_type->name;
        }
    }
    else if (auto pointer_type = dynamic_cast<PointerType*>(type)) {
        generateType(pointer_type->base_type.get());
        output << "*";
    }
    else if (auto list_type = dynamic_cast<ListType*>(type)) {
        output << "std::vector<";
        generateType(list_type->element_type.get());
        output << ">";
    }
    else if (auto dict_type = dynamic_cast<DictType*>(type)) {
        output << "std::map<";
        generateType(dict_type->key_type.get());
        output << ", ";
        generateType(dict_type->value_type.get());
        output << ">";
    }
    else {
        output << "auto";
    }
}

void CodeGenerator::generateExpression(Expr* expr) {
    if (!expr) {
        output << "/* null expression */";
        return;
    }

    // Handle CallExpr first - using the COMPREHENSIVE approach
    if (auto call_expr = dynamic_cast<CallExpr*>(expr)) {
        std::string function_name;

        // Extract function name whether it's direct call or member access
        if (auto ident = dynamic_cast<Identifier*>(call_expr->callee.get())) {
            function_name = ident->name;

            // Check if this might be a class constructor
            std::cout << "Generating call expression, might be constructor: " << function_name << std::endl;
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
        if (function_name == "print") {
            output << "std::cout";
            for (size_t i = 0; i < call_expr->arguments.size(); ++i) {
                output << " << ";
                generateExpression(call_expr->arguments[i].get());
            }
            output << " << std::endl";
            return;
        }
        else if (function_name == "input") {
            if (call_expr->arguments.size() > 0) {
                // Print prompt first, then get input
                output << "[]() { ";
                output << "std::cout << ";
                generateExpression(call_expr->arguments[0].get());
                output << " << std::flush; ";
                output << "std::string input; ";
                output << "std::getline(std::cin, input); ";
                output << "return input; }()";
            }
            else {
                output << "[]() { std::string input; std::getline(std::cin, input); return input; }()";
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
        else if (function_name == "int") {
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
        else if (function_name == "float") {
            if (call_expr->arguments.size() > 0) {
                output << "static_cast<float>(";
                generateExpression(call_expr->arguments[0].get());
                output << ")";
            }
            else {
                output << "0.0f";
            }
            return;
        }
        else if (function_name == "str") {
            if (call_expr->arguments.size() > 0) {
                output << "std::to_string(";
                generateExpression(call_expr->arguments[0].get());
                output << ")";
            }
            else {
                output << "\"\"";
            }
            return;
        }
        else if (function_name == "bool") {
            if (call_expr->arguments.size() > 0) {
                output << "static_cast<bool>(";
                generateExpression(call_expr->arguments[0].get());
                output << ")";
            }
            else {
                output << "false";
            }
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
        else if (function_name == "bin") {
            if (call_expr->arguments.size() > 0) {
                output << "[]() { std::stringstream ss; ss << std::hex << ";
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
                output << "[]() { std::stringstream ss; ss << std::oct << ";
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
                output << "[]() { std::stringstream ss; ss << std::hex << ";
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
                output << "[]() { std::vector<int> result; for(int i = 0; i < ";
                generateExpression(call_expr->arguments[0].get());
                output << "; i++) result.push_back(i); return result; }()";
            }
            else if (call_expr->arguments.size() == 2) {
                output << "[]() { std::vector<int> result; for(int i = ";
                generateExpression(call_expr->arguments[0].get());
                output << "; i < ";
                generateExpression(call_expr->arguments[1].get());
                output << "; i++) result.push_back(i); return result; }()";
            }
            else if (call_expr->arguments.size() == 3) {
                output << "[]() { std::vector<int> result; for(int i = ";
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
                output << "[]() { auto tmp = ";
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
                output << "[]() { auto tmp = ";
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
                output << "[]() { auto& container = ";
                generateExpression(call_expr->arguments[0].get());
                output << "; std::vector<std::pair<int, decltype(*container.begin())>> result; ";
                output << "int index = 0; for(auto& elem : container) { result.push_back({index++, elem}); } return result; }()";
            }
            else {
                output << "std::vector<std::pair<int, int>>()";
            }
            return;
        }
        else if (function_name == "zip") {
            if (call_expr->arguments.size() > 0) {
                output << "[]() { ";
                // Simplified zip implementation
                output << "// zip placeholder - returns empty vector ";
                output << "return std::vector<std::tuple<>>(); }()";
            }
            else {
                output << "std::vector<std::tuple<>>()";
            }
            return;
        }
        else if (function_name == "map") {
            if (call_expr->arguments.size() >= 2) {
                output << "[]() { auto& container = ";
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
                output << "[]() { auto& container = ";
                generateExpression(call_expr->arguments[1].get());
                output << "; std::vector<decltype(*container.begin())> result; ";
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
        else if (function_name == "randint") {
            if (call_expr->arguments.size() >= 2) {
                output << "[]() { return rand() % (";
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
                output << "[]() { auto& container = ";
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
                output << "[]() { auto tmp = ";
                generateExpression(call_expr->arguments[0].get());
                output << "; std::random_shuffle(tmp.begin(), tmp.end()); return tmp; }()";
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

        // Check if this is actually an array access (identifier with single argument)
        if (auto ident = dynamic_cast<Identifier*>(call_expr->callee.get())) {
            if (call_expr->arguments.size() == 1) {
                // This is array access, generate as array subscript
                output << ident->name << "[";
                generateExpression(call_expr->arguments[0].get());
                output << "]";
                return;
            }
        }

        // Handle constructor calls properly
        if (auto ident = dynamic_cast<Identifier*>(call_expr->callee.get())) {
            // Check if this identifier is a known class
            bool is_class_name = false;
            if (current_class && ident->name == current_class->name) {
                is_class_name = true;
            }
            // Also check other classes in scope (simplified)
            else if (ident->name == "Player" || ident->name == "Game" ||
                ident->name.find("Class") != std::string::npos) {
                is_class_name = true; // Heuristic
            }

            if (is_class_name) {
                // Generate constructor call - in C++ this is just ClassName()
                output << ident->name << "(";
                for (size_t i = 0; i < call_expr->arguments.size(); ++i) {
                    generateExpression(call_expr->arguments[i].get());
                    if (i < call_expr->arguments.size() - 1) {
                        output << ", ";
                    }
                }
                output << ")";
                return;
            }
        }

        // Handle member function calls (non-built-in)
        if (auto member_access = dynamic_cast<MemberAccess*>(call_expr->callee.get())) {
            generateExpression(member_access->object.get());

            // Use '.' for objects, '->' only for pointers/this
            if (auto ident = dynamic_cast<Identifier*>(member_access->object.get())) {
                if (ident->name == "self" && current_class) {
                    output << "->" << member_access->member << "(";
                }
                else if (ident->name == "this") {
                    output << "->" << member_access->member << "(";
                }
                else {
                    // For regular objects, use dot operator
                    output << "." << member_access->member << "(";
                }
            }
            else {
                // For complex expressions, default to dot operator (safer)
                output << "." << member_access->member << "(";
            }

            for (size_t i = 0; i < call_expr->arguments.size(); ++i) {
                generateExpression(call_expr->arguments[i].get());
                if (i < call_expr->arguments.size() - 1) {
                    output << ", ";
                }
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
                !dynamic_cast<CallExpr*>(bin_op->left.get());

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
        output << float_lit->value;
    }
    else if (auto str_lit = dynamic_cast<StringLiteral*>(expr)) {
        output << "\"" << str_lit->value << "\"";
    }
    else if (auto bool_lit = dynamic_cast<BoolLiteral*>(expr)) {
        output << (bool_lit->value ? "true" : "false");
    }
    // Enhanced member access handling for both objects and modules
    else if (auto member_access = dynamic_cast<MemberAccess*>(expr)) {
        // Check if this is module access (like greetings.hello)
        if (auto ident = dynamic_cast<Identifier*>(member_access->object.get())) {
            // Check if this identifier is a module (imported symbol)
            if (ident->name != "self" && ident->name != "this") {
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
            else {
                // For regular objects, use dot operator
                generateExpression(member_access->object.get());
                output << "." << member_access->member;
            }
        }
        else {
            // For complex expressions, use dot operator
            generateExpression(member_access->object.get());
            output << "." << member_access->member;
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
    output << getIndent() << "// pass\n";
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

void CodeGenerator::visit(ForStmt& node) {
    output << getIndent() << "for (auto " << node.var_name << " : ";
    if (node.iterable) {
        generateExpression(node.iterable.get());
    }
    output << ") {\n";
    indent();
    for (auto& stmt : node.body) {
        generateStatement(stmt.get());
    }
    dedent();
    output << getIndent() << "}\n";
}

void CodeGenerator::visit(UnsafeBlock& node) {
    output << getIndent() << "// unsafe {\n";
    indent();
    for (auto& stmt : node.body) {
        generateStatement(stmt.get());
    }
    dedent();
    output << getIndent() << "// } unsafe\n";
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
    output << node.value;
}

void CodeGenerator::visit(StringLiteral& node) {
    output << "\"" << node.value << "\"";
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