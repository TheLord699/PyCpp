#include "../include/log.h"
#include "../include/semantic_analyzer.h"
#include <iostream>
#include <stdexcept>

SemanticAnalyzer::SemanticAnalyzer()
    : current_scope(0), current_function(nullptr), current_class(nullptr) {
    // Start with global scope
    scopes.push_back(std::unordered_map<std::string, Symbol>());

    // Add built-in types
    addSymbol("int", "type");
    addSymbol("float", "type");
    addSymbol("double", "type");
    addSymbol("bool", "type");
    addSymbol("str", "type");
    addSymbol("string", "type");
    addSymbol("std::string", "type");
    addSymbol("char", "type");
    addSymbol("void", "type");
    addSymbol("auto", "type");

    // Add built-in functions
    addSymbol("print", "function");
    addSymbol("input", "function");
    addSymbol("len", "function");
    addSymbol("range", "function");
    addSymbol("enumerate", "function");
    addSymbol("zip", "function");
    addSymbol("map", "function");
    addSymbol("filter", "function");
    addSymbol("reduce", "function");
    addSymbol("sorted", "function");
    addSymbol("reversed", "function");
    addSymbol("min", "function");
    addSymbol("max", "function");
    addSymbol("sum", "function");
    addSymbol("abs", "function");
    addSymbol("round", "function");
    addSymbol("pow", "function");
    addSymbol("divmod", "function");
    addSymbol("bin", "function");
    addSymbol("oct", "function");
    addSymbol("hex", "function");
    addSymbol("chr", "function");
    addSymbol("ord", "function");
    addSymbol("type", "function");
    addSymbol("isinstance", "function");
    addSymbol("issubclass", "function");
    addSymbol("hasattr", "function");
    addSymbol("getattr", "function");
    addSymbol("setattr", "function");
    addSymbol("delattr", "function");
    addSymbol("property", "function");
    addSymbol("staticmethod", "function");
    addSymbol("classmethod", "function");
    addSymbol("mem.alloc", "function");
    addSymbol("mem.free", "function");
}

void SemanticAnalyzer::analyze(std::unique_ptr<Program>& program) {
    pycpp::debugLog() << "=== Starting Semantic Analysis ===" << std::endl;

    try {
        analyzeProgram(program.get());
        pycpp::debugLog() << "=== Semantic Analysis Completed Successfully ===" << std::endl;
        pycpp::debugLog() << "Detected built-in usage: ";
        for (const auto& builtin : used_builtins) {
            pycpp::debugLog() << builtin << " ";
        }
        pycpp::debugLog() << std::endl;
    }
    catch (const std::runtime_error& e) {
        throw;
    }
}

// Scope Management
void SemanticAnalyzer::enterScope() {
    scopes.push_back(std::unordered_map<std::string, Symbol>());
    current_scope++;
}

void SemanticAnalyzer::exitScope() {
    if (scopes.size() > 1) {
        scopes.pop_back();
        current_scope--;
    }
}

void SemanticAnalyzer::addSymbol(const std::string& name, const std::string& type, bool initialized) {
    if (symbolExistsInCurrentScope(name)) {
        error("Symbol '" + name + "' already declared in current scope", 0, 0);
        return;
    }

    Symbol symbol;
    symbol.name = name;
    symbol.type = type;
    symbol.is_initialized = initialized;
    symbol.scope_level = current_scope;

    scopes.back()[name] = symbol;
    pycpp::debugLog() << "Added symbol: " << name << " : " << type << " at scope " << current_scope << std::endl;
}

SemanticAnalyzer::Symbol* SemanticAnalyzer::findSymbol(const std::string& name) {
    // Search from current scope outward
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return &found->second;
        }
    }
    return nullptr;
}

bool SemanticAnalyzer::symbolExistsInCurrentScope(const std::string& name) {
    return scopes.back().find(name) != scopes.back().end();
}

// Type Checking - FIXED
bool SemanticAnalyzer::isValidType(const std::string& type) {
    static const std::unordered_set<std::string> valid_types = {
        "int", "float", "double", "bool", "str", "string", "void", "auto",
        "std::string"
    };

    // Check built-in types
    if (valid_types.find(type) != valid_types.end()) {
        return true;
    }

    // Check pointer types ("T*" as produced by typeToString)
    if (!type.empty() && type.back() == '*') {
        return isValidType(type.substr(0, type.size() - 1));
    }

    // Generic type parameters (T in def f[T] / class C[T])
    if (generic_params.count(type) > 0) {
        return true;
    }

    // Types brought in with `from module import Name`
    if (Symbol* imported = findSymbol(type)) {
        if (imported->type == "imported_symbol") return true;
    }

    // Check user-defined types (classes, structs)
    if (classes.find(type) != classes.end()) {
        return true;
    }

    // Check template types (basic check)
    if (type.find("std::vector<") == 0 || type.find("std::map<") == 0) {
        return true; // Simplified check
    }

    return false;
}

// Type Compatibility - FIXED
bool SemanticAnalyzer::typesAreCompatible(const std::string& type1, const std::string& type2) {
    // Basic type compatibility check
    if (type1 == type2) return true;
    if (type1 == "auto" || type2 == "auto") return true;

    // String compatibility - be more flexible
    if ((type1 == "str" && type2 == "string") ||
        (type1 == "string" && type2 == "str") ||
        (type1 == "str" && type2 == "std::string") ||
        (type1 == "std::string" && type2 == "str") ||
        (type1 == "string" && type2 == "std::string") ||
        (type1 == "std::string" && type2 == "string")) {
        return true;
    }

    // Numeric type conversions
    if ((type1 == "int" && type2 == "float") ||
        (type1 == "float" && type2 == "int") ||
        (type1 == "int" && type2 == "double") ||
        (type1 == "double" && type2 == "int")) {
        return true;
    }

    return false;
}

std::string SemanticAnalyzer::typeToString(Type* type) {
    if (!type) return "void";

    if (auto basic_type = dynamic_cast<BasicType*>(type)) {
        // Map PyCPP types to proper C++ types
        if (basic_type->name == "str") {
            return "std::string";
        }
        else if (basic_type->name == "std::string") {
            return "std::string";
        }
        return basic_type->name;
    }
    else if (auto pointer_type = dynamic_cast<PointerType*>(type)) {
        std::string base_type = typeToString(pointer_type->base_type.get());
        // Handle pointer types properly
        if (base_type == "std::string") {
            return "std::string*";
        }
        return base_type + "*";
    }
    else if (auto list_type = dynamic_cast<ListType*>(type)) {
        return "std::vector<" + typeToString(list_type->element_type.get()) + ">";
    }
    else if (auto dict_type = dynamic_cast<DictType*>(type)) {
        return "std::map<" + typeToString(dict_type->key_type.get()) + "," +
            typeToString(dict_type->value_type.get()) + ">";
    }
    return "auto";
}

std::string SemanticAnalyzer::getExpressionType(Expr* expr) {
    if (auto ident = dynamic_cast<Identifier*>(expr)) {
        // Built-in functions
        if (ident->name == "print") return "void";
        if (ident->name == "len") return "int";
        if (ident->name == "mem.alloc") return "void*";
        if (ident->name == "mem.free") return "void";

        Symbol* symbol = findSymbol(ident->name);
        return symbol ? symbol->type : "unknown";
    }
    else if (auto member_access = dynamic_cast<MemberAccess*>(expr)) {
        // Handle built-in memory functions
        if (auto ident = dynamic_cast<Identifier*>(member_access->object.get())) {
            if (ident->name == "mem") {
                if (member_access->member == "alloc") return "void*";
                if (member_access->member == "free") return "void";
            }
        }

        // For member access like self.health, check if it's a class member
        std::string object_type = getExpressionType(member_access->object.get());

        if (object_type == "auto" && current_class) {
            // In a class context, self.xxx should be valid
            ClassSymbol& class_sym = classes[current_class->name];
            auto it = class_sym.attributes.find(member_access->member);
            if (it != class_sym.attributes.end()) {
                return it->second.type;
            }
            return "auto"; // Return auto if member not found (will be handled by warning)
        }

        return "auto"; // Default to auto for member access
    }
    else if (auto int_lit = dynamic_cast<IntLiteral*>(expr)) {
        return "int";
    }
    else if (auto float_lit = dynamic_cast<FloatLiteral*>(expr)) {
        return "float";
    }
    else if (auto str_lit = dynamic_cast<StringLiteral*>(expr)) {
        return "str";
    }
    else if (auto bool_lit = dynamic_cast<BoolLiteral*>(expr)) {
        return "bool";
    }
    else if (auto list_lit = dynamic_cast<ListLiteral*>(expr)) {
        if (!list_lit->elements.empty()) {
            return "std::vector<" + getExpressionType(list_lit->elements[0].get()) + ">";
        }
        return "std::vector<auto>";
    }
    else if (auto bin_op = dynamic_cast<BinaryOp*>(expr)) {
        // For binary operations, return the type of the left operand
        return getExpressionType(bin_op->left.get());
    }
    else if (auto call = dynamic_cast<CallExpr*>(expr)) {
        if (auto ident = dynamic_cast<Identifier*>(call->callee.get())) {
            if (ident->name == "print") return "void";
            if (ident->name == "len") return "int";
            if (ident->name == "mem.alloc") return "void*";
            if (ident->name == "mem.free") return "void";
        }
        return "auto";
    }
    return "auto";
}

void SemanticAnalyzer::analyzeExpression(Expr* expr) {
    if (auto ident = dynamic_cast<Identifier*>(expr)) {
        // Check for built-in functions first
        if (ident->name == "print" || ident->name == "len" ||
            ident->name == "mem.alloc" || ident->name == "mem.free") {
            // Built-in functions are always valid
            trackBuiltinUsage(ident->name);
            if (ident->name == "print") {
                trackBuiltinUsage("iostream");
            }
            else if (ident->name == "mem.alloc" || ident->name == "mem.free") {
                trackBuiltinUsage("cstdlib");
            }
            pycpp::debugLog() << "Found built-in function: " << ident->name << std::endl;
            return;
        }

        // === FIX: Check if this might be a class name (constructor) ===
        if (classes.find(ident->name) != classes.end()) {
            // This is a class name used as constructor - valid
            pycpp::debugLog() << "Found class constructor call: " << ident->name << std::endl;
            return;
        }

        // === FIX: Check if this might be a function name ===
        if (functions.find(ident->name) != functions.end()) {
            // This is a function name - valid
            pycpp::debugLog() << "Found function call: " << ident->name << std::endl;
            return;
        }

        Symbol* symbol = findSymbol(ident->name);
        if (!symbol) {
            // Don't error immediately - it might be a class member accessed without 'self.'
            if (current_class) {
                // Check if it's a class member
                ClassSymbol& class_sym = classes[current_class->name];
                if (class_sym.attributes.find(ident->name) != class_sym.attributes.end()) {
                    pycpp::debugLog() << "Found class member (implicit self): " << ident->name
                        << " : " << class_sym.attributes[ident->name].type << std::endl;
                    return;
                }
            }

            // C++ qualified names (std::sqrt, std::string) come from `use <header>`
            if (ident->name.find("::") != std::string::npos) {
                return;
            }

            // === FIX: Don't error on unknown symbols during import processing ===
            // Just warn and continue - might be resolved by imports
            warning("Undefined symbol: " + ident->name, ident->line, ident->column);
            pycpp::debugLog() << "WARNING: Symbol '" << ident->name << "' not found, but continuing..." << std::endl;
        }
        else {
            pycpp::debugLog() << "Found identifier: " << ident->name << " : " << symbol->type << std::endl;
        }
    }
    else if (auto member_access = dynamic_cast<MemberAccess*>(expr)) {
        // Handle built-in memory functions: mem.alloc and mem.free
        if (auto ident = dynamic_cast<Identifier*>(member_access->object.get())) {
            if (ident->name == "mem") {
                if (member_access->member == "alloc" || member_access->member == "free") {
                    // These are built-in memory functions, not actual member access
                    trackBuiltinUsage("mem." + member_access->member);
                    trackBuiltinUsage("cstdlib");
                    pycpp::debugLog() << "Found built-in memory function: mem." << member_access->member << std::endl;
                    return;
                }
            }
        }

        // Handle regular member access like self.health
        analyzeExpression(member_access->object.get());

        // Check if the object is "self" in a class context
        if (auto ident = dynamic_cast<Identifier*>(member_access->object.get())) {
            if (ident->name == "self" && current_class) {
                // Check if the member exists in the class
                ClassSymbol& class_sym = classes[current_class->name];
                if (class_sym.attributes.find(member_access->member) == class_sym.attributes.end()) {
                    warning("Unknown member '" + member_access->member + "' in class '" +
                        current_class->name + "'", member_access->line, member_access->column);
                }
                else {
                    pycpp::debugLog() << "Found member access: self." << member_access->member <<
                        " : " << class_sym.attributes[member_access->member].type << std::endl;
                }
                return;
            }
        }

        // For other member access, just track it
        pycpp::debugLog() << "Found member access: " <<
            getExpressionType(member_access->object.get()) << "." << member_access->member << std::endl;
    }
    else if (auto int_lit = dynamic_cast<IntLiteral*>(expr)) {
        trackBuiltinUsage("int");
        pycpp::debugLog() << "Found integer literal: " << int_lit->value << std::endl;
    }
    else if (auto float_lit = dynamic_cast<FloatLiteral*>(expr)) {
        trackBuiltinUsage("float");
        pycpp::debugLog() << "Found float literal: " << float_lit->value << std::endl;
    }
    else if (auto str_lit = dynamic_cast<StringLiteral*>(expr)) {
        trackBuiltinUsage("str");
        trackBuiltinUsage("iostream"); // for potential output
        pycpp::debugLog() << "Found string literal: " << str_lit->value << std::endl;
    }
    else if (auto bool_lit = dynamic_cast<BoolLiteral*>(expr)) {
        trackBuiltinUsage("bool");
        pycpp::debugLog() << "Found bool literal: " << bool_lit->value << std::endl;
    }
    else if (auto list_lit = dynamic_cast<ListLiteral*>(expr)) {
        trackBuiltinUsage("vector");
        pycpp::debugLog() << "Found list literal with " << list_lit->elements.size() << " elements" << std::endl;
        for (auto& element : list_lit->elements) {
            analyzeExpression(element.get());
        }
    }
    else if (auto dict_lit = dynamic_cast<DictLiteral*>(expr)) {
        trackBuiltinUsage("map");
        pycpp::debugLog() << "Found dict literal with " << dict_lit->items.size() << " items" << std::endl;
        for (auto& item : dict_lit->items) {
            analyzeExpression(item.first.get());
            analyzeExpression(item.second.get());
        }
    }
    else if (auto bin_op = dynamic_cast<BinaryOp*>(expr)) {
        pycpp::debugLog() << "Found binary operation: " << bin_op->op << std::endl;

        // FIXED: Enhanced compound assignment handling
        if (bin_op->op == "+=" || bin_op->op == "-=" || bin_op->op == "*=" || bin_op->op == "/=") {
            trackBuiltinUsage("compound_ops");

            pycpp::debugLog() << "Compound assignment detected: " << bin_op->op << std::endl;

            // Analyze left side first to ensure it's a valid assignment target
            analyzeExpression(bin_op->left.get());

            // Check if left side is a valid assignment target
            if (!isValidAssignmentTarget(bin_op->left.get())) {
                error("Invalid assignment target for compound assignment",
                    bin_op->line, bin_op->column);
            }

            // Analyze right side
            analyzeExpression(bin_op->right.get());

            // Type checking for compound assignment
            std::string left_type = getExpressionType(bin_op->left.get());
            std::string right_type = getExpressionType(bin_op->right.get());

            if (!typesAreCompatible(left_type, right_type)) {
                warning("Type mismatch in compound assignment: " + left_type + " vs " + right_type,
                    bin_op->line, bin_op->column);
            }

            pycpp::debugLog() << "Compound assignment analysis completed" << std::endl;
        }
        else {
            // Regular binary operation (not compound assignment)
            analyzeExpression(bin_op->left.get());
            analyzeExpression(bin_op->right.get());

            // Type checking for regular binary operations
            std::string left_type = getExpressionType(bin_op->left.get());
            std::string right_type = getExpressionType(bin_op->right.get());

            if (!typesAreCompatible(left_type, right_type)) {
                warning("Type mismatch in binary operation: " + left_type + " vs " + right_type,
                    bin_op->line, bin_op->column);
            }
        }
    }
    else if (auto call = dynamic_cast<CallExpr*>(expr)) {
        pycpp::debugLog() << "Found function call" << std::endl;
        analyzeExpression(call->callee.get());
        for (auto& arg : call->arguments) {
            analyzeExpression(arg.get());
        }

        // Track built-in function calls
        if (auto ident = dynamic_cast<Identifier*>(call->callee.get())) {
            if (ident->name == "print") {
                trackBuiltinUsage("print");
                trackBuiltinUsage("iostream");
            }
            else if (ident->name == "len") {
                trackBuiltinUsage("len");
            }
            else if (ident->name == "mem.alloc" || ident->name == "mem.free") {
                trackBuiltinUsage("cstdlib");
            }
        }

        // Check if this is array access and validate the index
        if (auto ident = dynamic_cast<Identifier*>(call->callee.get())) {
            if (call->arguments.size() == 1) {
                // This might be array access, check if the identifier is an array
                Symbol* symbol = findSymbol(ident->name);
                if (symbol && symbol->type.find("std::vector<") == 0) {
                    // It's a vector, check if index is integer
                    std::string index_type = getExpressionType(call->arguments[0].get());
                    if (index_type != "int" && index_type != "auto") {
                        warning("Array index should be integer, got: " + index_type,
                            call->line, call->column);
                    }
                }
            }
        }
    }
    else if (auto unary_op = dynamic_cast<UnaryOp*>(expr)) {
        pycpp::debugLog() << "Found unary operation: " << unary_op->op << std::endl;
        analyzeExpression(unary_op->operand.get());
    }
    else if (auto ternary_op = dynamic_cast<TernaryOp*>(expr)) {
        pycpp::debugLog() << "Found ternary operation" << std::endl;
        analyzeExpression(ternary_op->condition.get());
        analyzeExpression(ternary_op->then_expr.get());
        analyzeExpression(ternary_op->else_expr.get());
    }
    else if (auto none_lit = dynamic_cast<NoneLiteral*>(expr)) {
        pycpp::debugLog() << "Found None literal" << std::endl;
        // None is valid, no analysis needed
    }
}

// ===== IMPORT HANDLING =====
void SemanticAnalyzer::analyzeImportStmt(ImportStmt* import) {
    pycpp::debugLog() << "Processing import: " << import->module;
    if (!import->imports.empty()) {
        pycpp::debugLog() << " (importing: ";
        for (const auto& item : import->imports) {
            pycpp::debugLog() << item << " ";
        }
        pycpp::debugLog() << ")";
    }
    pycpp::debugLog() << std::endl;

    // Add imported module to symbol table
    addSymbol(import->module, "module", true);
    
    // Add individual imports to symbol table
    for (const auto& item : import->imports) {
        addSymbol(item, "imported_symbol", true);
        pycpp::debugLog() << "Added imported symbol: " << item << std::endl;
    }
}

// Analysis Methods
void SemanticAnalyzer::analyzeProgram(Program* program) {
    pycpp::debugLog() << "Analyzing program with " << program->statements.size() << " statements" << std::endl;

    // Generic type parameters and constants are known before any body is analysed
    for (auto& stmt : program->statements) {
        if (auto func = dynamic_cast<FunctionDef*>(stmt.get())) {
            for (const auto& name : func->type_params) generic_params.insert(name);
        }
        else if (auto cls = dynamic_cast<ClassDef*>(stmt.get())) {
            for (const auto& name : cls->type_params) generic_params.insert(name);
        }
        else if (auto constant = dynamic_cast<ConstexprDef*>(stmt.get())) {
            addSymbol(constant->name, typeToString(constant->type.get()), true);
        }
    }

    // First pass: collect all classes, structs, and functions AND add them to global scope
    for (auto& stmt : program->statements) {
        if (auto func = dynamic_cast<FunctionDef*>(stmt.get())) {
            FunctionSymbol func_sym;
            func_sym.name = func->name;
            func_sym.return_type = typeToString(func->return_type.get());
            func_sym.has_return = false;

            for (const auto& param : func->parameters) {
                func_sym.parameters.emplace_back(param.name, typeToString(param.type.get()));
            }

            functions[func->name] = func_sym;

            // === FIX: Add function to global symbol table ===
            addSymbol(func->name, "function", true);
            pycpp::debugLog() << "Registered and added to symbol table function: " << func->name << " -> " << func_sym.return_type << std::endl;
        }
        else if (auto cls = dynamic_cast<ClassDef*>(stmt.get())) {
            ClassSymbol cls_sym;
            cls_sym.name = cls->name;
            cls_sym.base_class = cls->base_class;
            classes[cls->name] = cls_sym;

            // === FIX: Add class to global symbol table ===
            addSymbol(cls->name, "class", true);
            pycpp::debugLog() << "Registered and added to symbol table class: " << cls->name << std::endl;
        }
        else if (auto struc = dynamic_cast<StructDef*>(stmt.get())) {
            ClassSymbol cls_sym;
            cls_sym.name = struc->name;
            classes[struc->name] = cls_sym;

            // === FIX: Add struct to global symbol table ===
            addSymbol(struc->name, "class", true);
            pycpp::debugLog() << "Registered and added to symbol table struct: " << struc->name << std::endl;
        }
        else if (auto import = dynamic_cast<ImportStmt*>(stmt.get())) {
            pycpp::debugLog() << "Found import: " << import->module << std::endl;
        }
        else if (auto use = dynamic_cast<UseStmt*>(stmt.get())) {
            pycpp::debugLog() << "Found use statement: " << use->library << std::endl;
        }
    }

    // Second pass: analyze statements
    for (auto& stmt : program->statements) {
        analyzeStatement(stmt.get());
    }
}

void SemanticAnalyzer::analyzeStatement(Stmt* stmt) {
    if (auto expr_stmt = dynamic_cast<ExprStmt*>(stmt)) {
        analyzeExpression(expr_stmt->expr.get());
    }
    else if (auto var_decl = dynamic_cast<VarDecl*>(stmt)) {
        analyzeVarDecl(var_decl);
    }
    else if (auto func_def = dynamic_cast<FunctionDef*>(stmt)) {
        analyzeFunction(func_def);
    }
    else if (auto class_def = dynamic_cast<ClassDef*>(stmt)) {
        analyzeClass(class_def);
    }
    else if (auto struct_def = dynamic_cast<StructDef*>(stmt)) {
        analyzeStruct(struct_def);
    }
    else if (auto if_stmt = dynamic_cast<IfStmt*>(stmt)) {
        analyzeIfStmt(if_stmt);
    }
    else if (auto while_stmt = dynamic_cast<WhileStmt*>(stmt)) {
        analyzeWhileStmt(while_stmt);
    }
    else if (auto for_stmt = dynamic_cast<ForStmt*>(stmt)) {
        analyzeForStmt(for_stmt);
    }
    else if (auto return_stmt = dynamic_cast<ReturnStmt*>(stmt)) {
        analyzeReturnStmt(return_stmt);
    }
    else if (auto import_stmt = dynamic_cast<ImportStmt*>(stmt)) {
        analyzeImportStmt(import_stmt); // FIXED: Call the import analyzer
    }
    else if (auto use_stmt = dynamic_cast<UseStmt*>(stmt)) {
        // Use statements are for C++ headers
        pycpp::debugLog() << "Found use statement for library: " << use_stmt->library << std::endl;
    }
    else if (auto unsafe_block = dynamic_cast<UnsafeBlock*>(stmt)) {
        enterScope();
        for (auto& stmt : unsafe_block->body) {
            analyzeStatement(stmt.get());
        }
        exitScope();
    }
    else if (auto block = dynamic_cast<Block*>(stmt)) {
        enterScope();
        for (auto& stmt : block->statements) {
            analyzeStatement(stmt.get());
        }
        exitScope();
    }
}

// In SemanticAnalyzer::analyzeVarDecl - MODIFY the member variable handling:
void SemanticAnalyzer::analyzeVarDecl(VarDecl* var) {
    std::string type_name = typeToString(var->type.get());

    if (!isValidType(type_name)) {
        warning("Unknown type: " + type_name, var->line, var->column);
    }

    // For member variables (self.xxx), we need to store them differently
    if (var->var_name.find("self.") == 0) {
        // This is a member variable declaration in a class
        std::string member_name = var->var_name.substr(5); // Remove "self." prefix
        pycpp::debugLog() << "Processing member variable: " << member_name << " : " << type_name << std::endl;

        // Don't add to regular symbol table - it will be handled by class analysis
        if (current_class) {
            ClassSymbol& class_sym = classes[current_class->name];
            // Only add if not already present
            if (class_sym.attributes.find(member_name) == class_sym.attributes.end()) {
                class_sym.attributes[member_name] = {
                    member_name,
                    type_name,
                    var->value != nullptr,
                    0 // class scope
                };
                pycpp::debugLog() << "Stored class attribute: " << current_class->name << "." << member_name
                    << " : " << type_name << std::endl;
            }
            else {
                pycpp::debugLog() << "Class attribute already exists: " << current_class->name << "." << member_name << std::endl;
            }
        }
    }
    else {
        // === FIX: Check if we're in a struct context ===
        if (current_class && classes[current_class->name].is_struct) {
            // In struct context, don't add fields to global scope
            pycpp::debugLog() << "Skipping global scope addition for struct field: " << var->var_name << std::endl;
        }
        else {
            // Regular variable declaration - check if already declared in current scope
            if (symbolExistsInCurrentScope(var->var_name)) {
                // Check if it's the same declaration (line/column match)
                Symbol* existing = findSymbol(var->var_name);
                if (existing && existing->scope_level == current_scope) {
                    error("Symbol '" + var->var_name + "' already declared in current scope",
                        var->line, var->column);
                    return;
                }
            }
            addSymbol(var->var_name, type_name, var->value != nullptr);
        }
    }

    if (var->value) {
        analyzeExpression(var->value.get());
        std::string expr_type = getExpressionType(var->value.get());
        if (!typesAreCompatible(type_name, expr_type)) {
            warning("Type mismatch in variable initialization: " + type_name + " vs " + expr_type,
                var->line, var->column);
        }
    }

    pycpp::debugLog() << "Analyzed variable declaration: " << var->var_name << " : " << type_name << std::endl;
}

void SemanticAnalyzer::extractMembersFromConstructor(FunctionDef* constructor, ClassDef* cls) {
    if (!constructor || !cls) return;

    pycpp::debugLog() << "Extracting members from constructor for class: " << cls->name << std::endl;

    // Look for member assignments in constructor body
    for (auto& stmt : constructor->body) {
        // Handle variable declarations like: self.health: int = 100
        if (auto var_decl = dynamic_cast<VarDecl*>(stmt.get())) {
            if (var_decl->var_name.find("self.") == 0) {
                std::string member_name = var_decl->var_name.substr(5);
                std::string member_type = typeToString(var_decl->type.get());

                // Add to class attributes if not already present
                ClassSymbol& class_sym = classes[cls->name];
                if (class_sym.attributes.find(member_name) == class_sym.attributes.end()) {
                    class_sym.attributes[member_name] = {
                        member_name,
                        member_type,
                        var_decl->value != nullptr,
                        0
                    };
                    pycpp::debugLog() << "Extracted member from constructor declaration: "
                        << cls->name << "." << member_name << " : " << member_type << std::endl;
                }
            }
        }
        // Handle member assignments like: self.health = 100
        else if (auto expr_stmt = dynamic_cast<ExprStmt*>(stmt.get())) {
            if (auto bin_op = dynamic_cast<BinaryOp*>(expr_stmt->expr.get())) {
                if (bin_op->op == "=" || bin_op->op == "+=") { // Handle both assignment and compound assignment
                    if (auto member_access = dynamic_cast<MemberAccess*>(bin_op->left.get())) {
                        if (auto ident = dynamic_cast<Identifier*>(member_access->object.get())) {
                            if (ident->name == "self") {
                                std::string member_name = member_access->member;
                                std::string member_type = "auto"; // Infer type from assignment

                                // Try to infer type from the right side
                                if (bin_op->right) {
                                    std::string expr_type = getExpressionType(bin_op->right.get());
                                    if (expr_type != "auto" && expr_type != "unknown") {
                                        member_type = expr_type;
                                    }
                                }

                                // Add to class attributes if not already present
                                ClassSymbol& class_sym = classes[cls->name];
                                if (class_sym.attributes.find(member_name) == class_sym.attributes.end()) {
                                    class_sym.attributes[member_name] = {
                                        member_name,
                                        member_type,
                                        true, // Has initializer
                                        0
                                    };
                                    pycpp::debugLog() << "Extracted member from constructor assignment: "
                                        << cls->name << "." << member_name << " : " << member_type << std::endl;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void SemanticAnalyzer::analyzeFunction(FunctionDef* func) {
    pycpp::debugLog() << "Analyzing function: " << func->name << std::endl;

    // Set current function context
    // A method must not touch (or be confused with) a free function of the same name,
    // so it gets its own symbol, filled from its declaration.
    FunctionSymbol method_symbol;
    if (current_class) {
        method_symbol.name = func->name;
        method_symbol.return_type = typeToString(func->return_type.get());
        current_function = &method_symbol;
    }
    else {
        current_function = &functions[func->name];
    }
    current_function->has_return = false;

    enterScope(); // Function scope

    // Add parameters to scope
    for (const auto& param : func->parameters) {
        std::string param_type = typeToString(param.type.get());
        addSymbol(param.name, param_type, true);
        pycpp::debugLog() << "Added parameter to function scope: " << param.name << " : " << param_type << std::endl;
    }

    // FIXED: Single pass for analyzing function body
    // Don't pre-collect variable declarations separately
    for (auto& stmt : func->body) {
        analyzeStatement(stmt.get());
    }

    // Check return type - don't warn for constructors
    const std::string declared_return = typeToString(func->return_type.get());
    if (declared_return != "void" && declared_return != "None" && declared_return != "auto" &&
        !current_function->has_return &&
        func->name != "__init__") {  // Don't warn for constructors
        warning("Function '" + func->name + "' may not return a value", func->line, func->column);
    }

    exitScope(); // Exit function scope
    current_function = nullptr;
}

void SemanticAnalyzer::analyzeClass(ClassDef* cls) {
    pycpp::debugLog() << "Analyzing class: " << cls->name << std::endl;

    current_class = &classes[cls->name];

    // FIXED: Extract member variables from constructors first
    for (auto& method : cls->methods) {
        if (method->name == "__init__") {
            extractMembersFromConstructor(method.get(), cls);
        }
    }

    // Analyze attributes and add them to class symbol table
    for (auto& attr : cls->attributes) {
        analyzeVarDecl(attr.get());
    }

    // Analyze methods
    for (auto& method : cls->methods) {
        analyzeFunction(method.get());
    }

    current_class = nullptr;
}

void SemanticAnalyzer::analyzeStruct(StructDef* struc) {
    pycpp::debugLog() << "Analyzing struct: " << struc->name << std::endl;

    // Create a dedicated scope for this struct
    ClassSymbol& struct_sym = classes[struc->name];
    struct_sym.is_struct = true; // Mark as struct for code generation

    // Analyze fields but DON'T add them to global scope
    for (auto& field : struc->fields) {
        std::string field_name = field->var_name;
        std::string field_type = typeToString(field->type.get());

        // Add to struct's attribute map, not global scope
        struct_sym.attributes[field_name] = {
            field_name,
            field_type,
            field->value != nullptr,
            0 // struct scope
        };

        pycpp::debugLog() << "Added struct field: " << struc->name << "." << field_name
            << " : " << field_type << std::endl;

        // Analyze initializer expression if present
        if (field->value) {
            analyzeExpression(field->value.get());
        }
    }
}

// FIXED: Enhanced assignment target validation
bool SemanticAnalyzer::isValidAssignmentTarget(Expr* expr) {
    if (auto ident = dynamic_cast<Identifier*>(expr)) {
        return true;
    }

    // Allow member access (obj.attr)
    if (auto member_access = dynamic_cast<MemberAccess*>(expr)) {
        return true;
    }

    // Allow array subscript (arr[index])
    if (auto call = dynamic_cast<CallExpr*>(expr)) {
        if (auto ident = dynamic_cast<Identifier*>(call->callee.get())) {
            return true; // Assume it's array access
        }
    }

    return false;
}

// FIXED: Enhanced scope management for if statements
void SemanticAnalyzer::analyzeIfStmt(IfStmt* if_stmt) {
    analyzeExpression(if_stmt->condition.get());

    // Then branch - use separate scope
    enterScope();
    for (auto& stmt : if_stmt->then_branch) {
        analyzeStatement(stmt.get());
    }
    exitScope();

    // Elif branches - use separate scopes
    for (auto& elif_pair : if_stmt->elif_branches) {
        analyzeExpression(elif_pair.first.get());
        enterScope();
        for (auto& stmt : elif_pair.second) {
            analyzeStatement(stmt.get());
        }
        exitScope();
    }

    // Else branch - use separate scope
    if (!if_stmt->else_branch.empty()) {
        enterScope();
        for (auto& stmt : if_stmt->else_branch) {
            analyzeStatement(stmt.get());
        }
        exitScope();
    }
}

void SemanticAnalyzer::analyzeWhileStmt(WhileStmt* while_stmt) {
    analyzeExpression(while_stmt->condition.get());
    enterScope();
    for (auto& stmt : while_stmt->body) {
        analyzeStatement(stmt.get());
    }
    exitScope();
}

void SemanticAnalyzer::analyzeForStmt(ForStmt* for_stmt) {
    analyzeExpression(for_stmt->iterable.get());
    enterScope();
    if (for_stmt->var_names.empty()) {
        addSymbol(for_stmt->var_name, "auto", true);
    }
    else {
        for (const auto& name : for_stmt->var_names) addSymbol(name, "auto", true);
    }

    for (auto& stmt : for_stmt->body) {
        analyzeStatement(stmt.get());
    }

    exitScope();
}

void SemanticAnalyzer::analyzeReturnStmt(ReturnStmt* return_stmt) {
    if (current_function) {
        current_function->has_return = true;
        if (return_stmt->value) {
            analyzeExpression(return_stmt->value.get());

            // Check return type compatibility
            std::string return_type = getExpressionType(return_stmt->value.get());
            if (!typesAreCompatible(current_function->return_type, return_type)) {
                warning("Return type mismatch: " + current_function->return_type + " vs " + return_type,
                    return_stmt->line, return_stmt->column);
            }
        }
    }
    else {
        error("return statement outside function", return_stmt->line, return_stmt->column);
    }
}

// Built-in Usage Tracking
void SemanticAnalyzer::trackBuiltinUsage(const std::string& builtin) {
    used_builtins.insert(builtin);
}

// Error Reporting
static std::string location(int line, int column) {
    if (line <= 0) return "";
    std::string text = "line " + std::to_string(line);
    if (column > 0) text += ":" + std::to_string(column);
    return text + ": ";
}

void SemanticAnalyzer::error(const std::string& message, int line, int column) {
    throw std::runtime_error(location(line, column) + "semantic error: " + message); // reported once, by the caller
}

void SemanticAnalyzer::warning(const std::string& message, int line, int column) {
    std::cerr << "warning: " << location(line, column) << message << std::endl;
}

// Visitor pattern implementations (delegate to analysis methods)
void SemanticAnalyzer::visit(Program& node) { analyzeProgram(&node); }
void SemanticAnalyzer::visit(FunctionDef& node) { analyzeFunction(&node); }
void SemanticAnalyzer::visit(ClassDef& node) { analyzeClass(&node); }
void SemanticAnalyzer::visit(StructDef& node) { analyzeStruct(&node); }
void SemanticAnalyzer::visit(TemplateDef& node) { /* Template analysis */ }
void SemanticAnalyzer::visit(ConstexprDef& node) { /* Constexpr analysis */ }
void SemanticAnalyzer::visit(VarDecl& node) { analyzeVarDecl(&node); }
void SemanticAnalyzer::visit(ReturnStmt& node) { analyzeReturnStmt(&node); }
void SemanticAnalyzer::visit(PassStmt& node) { /* No analysis needed */ }
void SemanticAnalyzer::visit(BreakStmt& node) { /* No analysis needed */ }
void SemanticAnalyzer::visit(ContinueStmt& node) { /* No analysis needed */ }
void SemanticAnalyzer::visit(IfStmt& node) { analyzeIfStmt(&node); }
void SemanticAnalyzer::visit(WhileStmt& node) { analyzeWhileStmt(&node); }
void SemanticAnalyzer::visit(ForStmt& node) { analyzeForStmt(&node); }
void SemanticAnalyzer::visit(UnsafeBlock& node) {
    enterScope();
    for (auto& stmt : node.body) { stmt->accept(*this); }
    exitScope();
}
void SemanticAnalyzer::visit(ExprStmt& node) { analyzeExpression(node.expr.get()); }
void SemanticAnalyzer::visit(Block& node) {
    enterScope();
    for (auto& stmt : node.statements) { stmt->accept(*this); }
    exitScope();
}
void SemanticAnalyzer::visit(IntLiteral& node) { trackBuiltinUsage("int"); }
void SemanticAnalyzer::visit(FloatLiteral& node) { trackBuiltinUsage("float"); }
void SemanticAnalyzer::visit(StringLiteral& node) { trackBuiltinUsage("str"); trackBuiltinUsage("iostream"); }
void SemanticAnalyzer::visit(BoolLiteral& node) { trackBuiltinUsage("bool"); }
void SemanticAnalyzer::visit(NoneLiteral& node) { /* No analysis needed */ }
void SemanticAnalyzer::visit(Identifier& node) { analyzeExpression(&node); }
void SemanticAnalyzer::visit(BinaryOp& node) { analyzeExpression(&node); }
void SemanticAnalyzer::visit(UnaryOp& node) { analyzeExpression(node.operand.get()); }
void SemanticAnalyzer::visit(TernaryOp& node) {
    analyzeExpression(node.condition.get());
    analyzeExpression(node.then_expr.get());
    analyzeExpression(node.else_expr.get());
}
void SemanticAnalyzer::visit(CallExpr& node) { analyzeExpression(&node); }
void SemanticAnalyzer::visit(ListLiteral& node) {
    trackBuiltinUsage("vector");
    for (auto& elem : node.elements) { analyzeExpression(elem.get()); }
}
void SemanticAnalyzer::visit(DictLiteral& node) {
    trackBuiltinUsage("map");
    for (auto& item : node.items) {
        analyzeExpression(item.first.get());
        analyzeExpression(item.second.get());
    }
}
void SemanticAnalyzer::visit(MemberAccess& node) { analyzeExpression(&node); }
void SemanticAnalyzer::visit(BasicType& node) { /* Type analysis handled elsewhere */ }
void SemanticAnalyzer::visit(PointerType& node) { /* Type analysis handled elsewhere */ }
void SemanticAnalyzer::visit(ListType& node) { /* Type analysis handled elsewhere */ }
void SemanticAnalyzer::visit(DictType& node) { /* Type analysis handled elsewhere */ }
void SemanticAnalyzer::visit(ImportStmt& node) { analyzeImportStmt(&node); } // FIXED: Added import visitor
void SemanticAnalyzer::visit(UseStmt& node) { /* Use statement analysis */ }