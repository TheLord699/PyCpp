#include "../include/log.h"
#include "../include/semantic_analyzer.h"
#include "../include/diagnostics.h"
#include "../include/builtin_methods.h"
#include <algorithm>
#include <functional>
#include <cstring>
#include <iostream>
#include <stdexcept>

// C++ type names -> the names the user wrote (std::vector<std::string> -> list[str])
static std::string friendlyType(const std::string& t) {
    struct Rep { const char* from; const char* to; bool opens; };
    static const Rep reps[] = {
        {"std::vector<", "list[", true}, {"std::map<", "dict[", true}, {"std::tuple<", "tuple[", true},
        {"std::string", "str", false},
        {"int8_t", "i8", false}, {"int16_t", "i16", false},
        {"int32_t", "i32", false}, {"int64_t", "i64", false},
    };
    std::string out;
    int depth = 0;
    for (size_t i = 0; i < t.size();) {
        bool hit = false;
        for (const auto& r : reps) {
            const size_t n = std::strlen(r.from);
            if (t.compare(i, n, r.from) == 0) {
                out += r.to;
                if (r.opens) depth++;
                i += n;
                hit = true;
                break;
            }
        }
        if (hit) continue;
        if (t[i] == '>' && depth > 0) { out += ']'; depth--; }
        else out += t[i];
        ++i;
    }
    return out;
}

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
    addSymbol("assert", "function");
    for (const char* name : { "sqrt", "sin", "cos", "tan", "log", "log10", "exp",
                              "ceil", "floor", "randint", "choice", "shuffle" }) {
        addSymbol(name, "function");
    }
    addSymbol("true", "bool");
    addSymbol("false", "bool");
    addSymbol("nullptr", "void*");
    // [diag-fix] builtins the lexer knows, so they are never reported as unknown names
    for (const char* name : { "exit", "quit", "help", "dir", "vars", "locals", "globals", "id", "hash",
                              "del", "gc", "malloc", "free", "open", "read", "write", "close", "seek", "tell",
                              "format", "join", "split", "strip", "replace", "find", "startswith", "endswith",
                              "lower", "upper", "capitalize", "title", "math", "random", "uniform" }) {
        addSymbol(name, "function");
    }
    for (const char* name : { "Exception", "ArithmeticError", "ZeroDivisionError", "OverflowError",
                              "LookupError", "IndexError", "KeyError", "ValueError", "TypeError",
                              "RuntimeError" }) {
        addSymbol(name, "class");
    }
    for (const char* name : { "list", "dict", "set", "tuple", "any", "all", "repr" }) {   // [fix.py]
        addSymbol(name, "function");
    }
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
        // [fix2] variables that were declared but never mentioned again
        std::vector<const Symbol*> unused;
        for (const auto& kv : scopes.back())
            if (kv.second.is_var && !kv.second.used && !kv.first.empty() && kv.first[0] != '_')
                unused.push_back(&kv.second);
        std::sort(unused.begin(), unused.end(), [](const Symbol* a, const Symbol* b) { return a->line < b->line; });
        for (const auto& kv : scopes.back())
            if (kv.second.is_var) ended_names[kv.first] = kv.second.line;
        for (const Symbol* s : unused)
            warning("W0001", "unused variable '" + s->name + "'", s->line, s->column,
                    "if this is intentional, rename it to '_" + s->name + "'", static_cast<int>(s->name.size()));
        scopes.pop_back();
        current_scope--;
    }
}

void SemanticAnalyzer::addSymbol(const std::string& name, const std::string& type, bool initialized, int line, int column) {
    if (symbolExistsInCurrentScope(name)) {
        error("E0103", "the name '" + name + "' is defined more than once in this scope", line, column,
              "rename one of them, or remove the duplicate definition", static_cast<int>(name.size()));
        return;
    }

    Symbol symbol;
    symbol.name = name;
    symbol.type = type;
    symbol.is_initialized = initialized;
    symbol.scope_level = current_scope;
    symbol.line = line;
    symbol.column = column;   // [fix2]

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

// Exception types provided by the runtime
static bool isBuiltinException(const std::string& name) {
    static const std::unordered_set<std::string> names = {
        "Exception", "ArithmeticError", "ZeroDivisionError", "OverflowError",
        "LookupError", "IndexError", "KeyError", "ValueError", "TypeError", "RuntimeError"
    };
    return names.count(name) > 0;
}

// Type Checking - FIXED
bool SemanticAnalyzer::isValidType(const std::string& type) {
    static const std::unordered_set<std::string> valid_types = {
        "int", "float", "double", "bool", "str", "string", "void", "auto",
        "std::string", "char",
        "int8_t", "int16_t", "int32_t", "int64_t"
    };

    // Check built-in types
    if (valid_types.find(type) != valid_types.end()) {
        return true;
    }

    // Check pointer types ("T*" as produced by typeToString)
    if (!type.empty() && type.back() == '*') {
        return isValidType(type.substr(0, type.size() - 1));
    }

    if (isBuiltinException(type)) {
        return true;
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
    if ((type.find("std::vector<") == 0 || type.find("std::map<") == 0 || type.find("std::set<") == 0 ||
         type.find("std::tuple<") == 0) &&
        type.back() == '>') {
        // [fix2] check the element types too: list[Playr] is an error, not "simplified"
        const size_t open = type.find('<');
        const std::string inner = type.substr(open + 1, type.size() - open - 2);
        int depth = 0;
        size_t start = 0;
        bool ok = true;
        for (size_t i = 0; i <= inner.size(); ++i) {
            const char c = i < inner.size() ? inner[i] : ',';
            if (c == '<') depth++;
            else if (c == '>') depth--;
            else if (c == ',' && depth == 0) {
                if (!isValidType(inner.substr(start, i - start))) ok = false;
                start = i + 1;
            }
        }
        return ok;
    }

    return false;
}

// "std::tuple<A,std::map<K,V>>" -> { A, std::map<K,V> }
static std::vector<std::string> splitTypeArgs(const std::string& t) {
    std::vector<std::string> out;
    const size_t open = t.find('<');
    if (open == std::string::npos || t.back() != '>') return out;
    const std::string inner = t.substr(open + 1, t.size() - open - 2);
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i <= inner.size(); ++i) {
        const char c = i < inner.size() ? inner[i] : ',';
        if (c == '<') depth++;
        else if (c == '>') depth--;
        else if (c == ',' && depth == 0) {
            out.push_back(inner.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

// Type Compatibility
static bool isNumericType(const std::string& t) {
    static const std::unordered_set<std::string> numeric = {
        "int", "float", "double", "bool", "char",
        "int8_t", "int16_t", "int32_t", "int64_t"
    };
    return numeric.count(t) > 0;
}

bool SemanticAnalyzer::typesAreCompatible(const std::string& type1, const std::string& type2) {
    if (type1 == type2) return true;
    if (type1 == "auto" || type2 == "auto") return true;
    if (type1 == "unknown" || type2 == "unknown") return true;
    if (generic_params.count(type1) || generic_params.count(type2)) return true;

    auto isStr = [](const std::string& t) {
        return t == "str" || t == "string" || t == "std::string";
    };
    if (isStr(type1) && isStr(type2)) return true;

    // C++ semantics: arithmetic types convert implicitly
    if (isNumericType(type1) && isNumericType(type2)) return true;

    // list[T]: compare element types ("std::vector<auto>" matches anything)
    static const std::string vec = "std::vector<";
    if (type1.rfind(vec, 0) == 0 && type2.rfind(vec, 0) == 0 &&
        type1.back() == '>' && type2.back() == '>') {
        return typesAreCompatible(type1.substr(vec.size(), type1.size() - vec.size() - 1),
                                  type2.substr(vec.size(), type2.size() - vec.size() - 1));
    }

    // tuple[A, B]: same number of elements, each compatible
    if (type1.rfind("std::tuple<", 0) == 0 && type2.rfind("std::tuple<", 0) == 0 &&
        type1.back() == '>' && type2.back() == '>') {
        const auto a = splitTypeArgs(type1);
        const auto b = splitTypeArgs(type2);
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (!typesAreCompatible(a[i], b[i])) return false;
        return true;
    }

    // None converts to any pointer type
    if ((type1 == "None" && !type2.empty() && type2.back() == '*') ||
        (type2 == "None" && !type1.empty() && type1.back() == '*')) return true;

    // A derived class (or pointer to one) where its base is expected
    {
        std::string a = type1, b = type2;
        if (!a.empty() && a.back() == '*' && !b.empty() && b.back() == '*') { a.pop_back(); b.pop_back(); }
        if (isSubclassOf(b, a)) return true;
    }

    // void* converts to any pointer type
    if ((type1 == "void*" && !type2.empty() && type2.back() == '*') ||
        (type2 == "void*" && !type1.empty() && type1.back() == '*')) return true;

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
        if (basic_type->name == "std::tuple" && !basic_type->type_args.empty()) {
            std::string out = "std::tuple<";
            for (size_t i = 0; i < basic_type->type_args.size(); ++i)
                out += (i ? "," : "") + typeToString(basic_type->type_args[i].get());
            return out + ">";
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

static bool isStrType(const std::string& t) {
    return t == "str" || t == "string" || t == "std::string";
}
static bool isIntegerType(const std::string& t) {
    static const std::unordered_set<std::string> ints = {
        "int", "int8_t", "int16_t", "int32_t", "int64_t", "char" };
    return ints.count(t) > 0;
}
static bool isFloatType(const std::string& t) { return t == "float" || t == "double"; }
static bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

// "std::map<K,V>" -> {K, V} (split at the top-level comma)
static std::pair<std::string, std::string> mapTypes(const std::string& t) {
    const size_t open = t.find('<');
    const std::string inner = t.substr(open + 1, t.size() - open - 2);
    int depth = 0;
    for (size_t i = 0; i < inner.size(); ++i) {
        if (inner[i] == '<') depth++;
        else if (inner[i] == '>') depth--;
        else if (inner[i] == ',' && depth == 0) return { inner.substr(0, i), inner.substr(i + 1) };
    }
    return { "auto", "auto" };
}

// Which built-in container a type string is, if any.
static bool receiverOf(const std::string& t, pycpp::Receiver& r) {
    if (isStrType(t)) { r = pycpp::Receiver::Str; return true; }
    if (startsWith(t, "std::vector<")) { r = pycpp::Receiver::List; return true; }
    if (startsWith(t, "std::map<")) { r = pycpp::Receiver::Dict; return true; }
    if (startsWith(t, "std::set<")) { r = pycpp::Receiver::Set; return true; }
    return false;
}

std::string SemanticAnalyzer::getExpressionType(Expr* expr) {
    if (!expr) return "void";
    auto cached = expr_types.find(expr);
    if (cached != expr_types.end()) return cached->second;

    if (auto ident = dynamic_cast<Identifier*>(expr)) {
        // Built-in functions
        if (ident->name == "print") return "void";
        if (ident->name == "len") return "int";
        if (ident->name == "mem.alloc") return "void*";
        if (ident->name == "mem.free") return "void";
        if (ident->name == "self" && current_class) return current_class->name;

        Symbol* symbol = findSymbol(ident->name);
        if (symbol) return symbol->type;
        if (current_class) {   // a member used without `self.`
            if (const Symbol* attr = findAttribute(current_class->name, ident->name)) return attr->type;
        }
        return "unknown";
    }
    else if (auto member_access = dynamic_cast<MemberAccess*>(expr)) {
        // Handle built-in memory functions
        if (auto ident = dynamic_cast<Identifier*>(member_access->object.get())) {
            if (ident->name == "mem") {
                if (member_access->member == "alloc") return "void*";
                if (member_access->member == "free") return "void";
            }
        }
        std::string object_type = getExpressionType(member_access->object.get());
        if (!object_type.empty() && object_type.back() == '*') object_type.pop_back();   // p.x through a pointer
        if (const Symbol* attr = findAttribute(object_type, member_access->member)) return attr->type;
        return "auto";
    }
    else if (auto int_lit = dynamic_cast<IntLiteral*>(expr)) {
        return (int_lit->value > 2147483647LL || int_lit->value < -2147483648LL) ? "int64_t" : "int";
    }
    else if (dynamic_cast<FloatLiteral*>(expr)) {
        return "float";
    }
    else if (dynamic_cast<StringLiteral*>(expr) || dynamic_cast<FString*>(expr)) {
        return "str";
    }
    else if (dynamic_cast<BoolLiteral*>(expr)) {
        return "bool";
    }
    else if (dynamic_cast<NoneLiteral*>(expr)) {
        return "None";
    }
    else if (dynamic_cast<Lambda*>(expr)) {
        return "auto";
    }
    else if (auto comp = dynamic_cast<Comprehension*>(expr)) {
        return "auto";   // the analysed type is cached in expr_types
    }
    else if (auto tuple_lit = dynamic_cast<TupleLiteral*>(expr)) {
        std::string out = "std::tuple<";
        for (size_t i = 0; i < tuple_lit->elements.size(); ++i) {
            std::string t = getExpressionType(tuple_lit->elements[i].get());
            if (t == "str") t = "std::string";
            if (t == "unknown" || t == "void" || t == "None") t = "auto";
            out += (i ? "," : "") + t;
        }
        return out + ">";
    }
    else if (auto list_lit = dynamic_cast<ListLiteral*>(expr)) {
        if (!list_lit->elements.empty()) {
            std::string elem = getExpressionType(list_lit->elements[0].get());
            if (elem == "str") elem = "std::string";
            return "std::vector<" + elem + ">";
        }
        return "std::vector<auto>";
    }
    else if (auto dict_lit = dynamic_cast<DictLiteral*>(expr)) {
        if (dict_lit->items.empty()) return "std::map<auto,auto>";
        std::string k = getExpressionType(dict_lit->items[0].first.get());
        std::string v = getExpressionType(dict_lit->items[0].second.get());
        if (k == "str") k = "std::string";
        if (v == "str") v = "std::string";
        return "std::map<" + k + "," + v + ">";
    }
    else if (auto unary = dynamic_cast<UnaryOp*>(expr)) {
        std::string inner = getExpressionType(unary->operand.get());
        if (unary->op == "*") {
            if (!inner.empty() && inner.back() == '*') return inner.substr(0, inner.size() - 1);
            return "auto";
        }
        if (unary->op == "&") return (inner == "auto" || inner == "unknown") ? "auto" : inner + "*";
        if (unary->op == "not") return "bool";
        return inner;
    }
    else if (auto ternary = dynamic_cast<TernaryOp*>(expr)) {
        return getExpressionType(ternary->then_expr.get());
    }
    else if (auto bin_op = dynamic_cast<BinaryOp*>(expr)) {
        static const std::unordered_set<std::string> bool_ops = {
            "<", ">", "<=", ">=", "==", "!=", "and", "or", "is", "is not", "in", "not in"
        };
        if (bool_ops.count(bin_op->op)) return "bool";
        const std::string left = getExpressionType(bin_op->left.get());
        if (bin_op->op == "/") {   // true division: integers give a double, as in Python
            const std::string right = getExpressionType(bin_op->right.get());
            if (isFloatType(left)) return left;
            if (isFloatType(right)) return right;
            if (isIntegerType(left) || left == "bool") return "double";
            return left;
        }
        if (bin_op->op == "+" || bin_op->op == "-" || bin_op->op == "*") {
            const std::string right = getExpressionType(bin_op->right.get());
            if (isIntegerType(left) && isFloatType(right)) return right;   // 1 + 2.5 is a float
            if (left == "float" && right == "double") return right;
        }
        // otherwise the type of the left operand
        return left;
    }
    else if (auto call = dynamic_cast<CallExpr*>(expr)) {
        if (call->is_subscript) { // a[i]
            std::string container = getExpressionType(call->callee.get());
            if (startsWith(container, "std::vector<") && container.back() == '>')
                return container.substr(12, container.size() - 13);
            if (startsWith(container, "std::map<") && container.back() == '>')
                return mapTypes(container).second;
            if (startsWith(container, "std::tuple<") && container.back() == '>') {
                if (auto idx = dynamic_cast<IntLiteral*>(call->arguments[0].get())) {
                    const auto parts = splitTypeArgs(container);
                    if (idx->value >= 0 && idx->value < static_cast<long long>(parts.size()))
                        return parts[static_cast<size_t>(idx->value)];
                }
                return "auto";
            }
            if (isStrType(container)) return "char";
            if (!container.empty() && container.back() == '*')
                return container.substr(0, container.size() - 1);
            return "auto";
        }
        if (auto mem = dynamic_cast<MemberAccess*>(call->callee.get())) {
            auto base = dynamic_cast<Identifier*>(mem->object.get());
            if (base && base->name == "mem" && mem->member == "alloc" && !call->type_args.empty())
                return typeToString(call->type_args[0].get()) + "*"; // mem.alloc[T]() -> T*

            std::string object_type = getExpressionType(mem->object.get());
            if (!object_type.empty() && object_type.back() == '*') object_type.pop_back();
            if (const FunctionSymbol* m = findMethod(object_type, mem->member)) return m->return_type;

            pycpp::Receiver r;
            if (receiverOf(object_type, r)) {
                if (const pycpp::BuiltinMethod* bm = pycpp::findBuiltinMethod(r, mem->member)) {
                    const std::string res = bm->result;
                    if (res == "same") return object_type;
                    if (res == "str" || res == "int" || res == "bool" || res == "void") return res;
                    if (res == "list[str]") return "std::vector<std::string>";
                    if (res == "elem") return elementTypeOf(object_type);
                    if (r == pycpp::Receiver::Dict) {
                        const auto kv = mapTypes(object_type);
                        if (res == "key") return kv.first;
                        if (res == "value") return kv.second;
                        if (res == "list[key]") return "std::vector<" + kv.first + ">";
                        if (res == "list[value]") return "std::vector<" + kv.second + ">";
                    }
                }
            }
            return "auto";
        }
        if (auto inner = dynamic_cast<CallExpr*>(call->callee.get())) {
            (void)inner;
            return "auto";
        }
        if (auto ident = dynamic_cast<Identifier*>(call->callee.get())) {
            const std::string& n = ident->name;
            if (n == "super" && current_class) return current_class->base_class.empty() ? "auto" : current_class->base_class;
            if (n == "print") return "void";
            if (n == "any" || n == "all") return "bool";   // [fix.py]
            if (n == "repr") return "std::string";
            if (n == "type") return "auto";
            if (n == "len" || n == "ord") return "int";
            if (n == "str" || n == "chr" || n == "bin" || n == "oct" || n == "hex" || n == "input") return "std::string";
            if (n == "int") return "int";
            if (n == "float") return "float";
            if (n == "bool") return "bool";
            if (n == "mem.free") return "void";
            if (n == "range") return "std::vector<int>";
            if ((n == "sorted" || n == "reversed" || n == "list") && call->arguments.size() == 1) {
                const std::string arg = getExpressionType(call->arguments[0].get());
                if (startsWith(arg, "std::vector<")) return arg;
                return "auto";
            }
            if ((n == "min" || n == "max" || n == "sum") && call->arguments.size() == 1)
                return elementTypeOf(getExpressionType(call->arguments[0].get()));
            if ((n == "min" || n == "max" || n == "abs") && !call->arguments.empty())
                return getExpressionType(call->arguments[0].get());
            auto fn = functions.find(n);
            if (fn != functions.end()) {
                Symbol* bound = findSymbol(n);
                if (!bound || bound->type == "function") return fn->second.return_type; // user function
            }
            if (classes.count(n)) return n;                           // constructor
        }
        return "auto";
    }
    return "auto";
}

void SemanticAnalyzer::analyzeExpression(Expr* expr) {
    if (!expr) return;
    if (auto ident = dynamic_cast<Identifier*>(expr)) {
        // Check for built-in functions first
        if (ident->name == "print" || ident->name == "len" ||
            ident->name == "mem.alloc" || ident->name == "mem.free") {
            trackBuiltinUsage(ident->name);
            if (ident->name == "print") trackBuiltinUsage("iostream");
            else if (ident->name == "mem.alloc" || ident->name == "mem.free") trackBuiltinUsage("cstdlib");
            return;
        }
        if (ident->name == "super") {
            errorAt("E0113", "'super' must be called: use `super().method(...)`", ident);
        }

        // class names (constructors) and function names
        if (classes.find(ident->name) != classes.end()) return;
        if (functions.find(ident->name) != functions.end() && !findSymbol(ident->name)) return;

        Symbol* symbol = findSymbol(ident->name);
        if (!symbol) {
            // a class member accessed without 'self.'
            if (current_class && findAttribute(current_class->name, ident->name)) return;

            // C++ qualified names (std::sqrt, std::string) come from `use <header>`
            if (ident->name.find("::") != std::string::npos) return;

            if (!has_cpp_headers) {
                std::vector<std::string> known;
                for (auto& scope : scopes) for (auto& kv : scope) known.push_back(kv.first);
                for (auto& kv : functions) known.push_back(kv.first);
                for (auto& kv : classes) known.push_back(kv.first);
                std::string hint;
                auto ended = ended_names.find(ident->name);
                if (ended != ended_names.end()) {
                    hint = "'" + ident->name + "' was declared at line " + std::to_string(ended->second) +
                           " inside a block that has already ended; declare it before that block "
                           "(e.g. `" + ident->name + ": int = 0`) so it stays visible here";
                }
                else {
                    const std::string guess = pycpp::closestMatch(ident->name, known);
                    hint = guess.empty() ? std::string("check the spelling, or declare it before it is used")
                                         : "a similar name exists, did you mean '" + guess + "'?";
                }
                error("E0101", "cannot find '" + ident->name + "' in this scope", ident->line, ident->column,
                      hint, static_cast<int>(ident->name.size()));
            }
        }
        else {
            symbol->used = true;   // [fix2]
        }
    }
    else if (auto member_access = dynamic_cast<MemberAccess*>(expr)) {
        if (auto ident = dynamic_cast<Identifier*>(member_access->object.get())) {
            if (ident->name == "mem" && !findSymbol("mem") &&
                (member_access->member == "alloc" || member_access->member == "free")) {
                trackBuiltinUsage("mem." + member_access->member);
                trackBuiltinUsage("cstdlib");
                return;
            }
        }

        if (auto super_call = dynamic_cast<CallExpr*>(member_access->object.get()); super_call && isSuperCall(super_call)) {
            checkSuper(super_call);
            const std::string base = current_class ? current_class->base_class : "";
            if (classFullyKnown(base) && !findMethod(base, member_access->member) &&
                !findAttribute(base, member_access->member)) {
                errorAt("E0112", "the base class '" + base + "' has no method or attribute '" + member_access->member + "'",
                        member_access);
            }
            return;
        }

        analyzeExpression(member_access->object.get());

        // self.member inside a class
        if (auto ident = dynamic_cast<Identifier*>(member_access->object.get())) {
            if (ident->name == "self" && current_class) {
                ClassSymbol& class_sym = classes[current_class->name];
                if (!findAttribute(current_class->name, member_access->member) &&
                    !findMethod(current_class->name, member_access->member) &&
                    current_class->base_class.empty()) {   // the base may live in another module
                    std::vector<std::string> known;
                    for (const auto& kv : class_sym.attributes) known.push_back(kv.first);
                    for (const auto& kv : class_sym.methods) known.push_back(kv.first);
                    const std::string guess = pycpp::closestMatch(member_access->member, known);
                    warning("W0006", "no member named '" + member_access->member + "' in class '" + current_class->name + "'",
                        member_access->line, member_access->column,
                        guess.empty() ? "declare it with `self." + member_access->member + ": <type> = ...` in __init__"
                                      : "did you mean 'self." + guess + "'?",
                        static_cast<int>(member_access->member.size()));
                }
            }
        }
    }
    else if (auto int_lit = dynamic_cast<IntLiteral*>(expr)) {
        (void)int_lit;
        trackBuiltinUsage("int");
    }
    else if (dynamic_cast<FloatLiteral*>(expr)) {
        trackBuiltinUsage("float");
    }
    else if (dynamic_cast<StringLiteral*>(expr)) {
        trackBuiltinUsage("str");
        trackBuiltinUsage("iostream");
    }
    else if (auto fstr = dynamic_cast<FString*>(expr)) {
        trackBuiltinUsage("str");
        for (auto& part : fstr->parts) {
            if (!part.expr) continue;
            analyzeExpression(part.expr.get());
            if (getExpressionType(part.expr.get()) == "void")
                errorAt("E0104", "this f-string field has no value: the function returns nothing", part.expr.get());
        }
    }
    else if (dynamic_cast<BoolLiteral*>(expr)) {
        trackBuiltinUsage("bool");
    }
    else if (auto lambda = dynamic_cast<Lambda*>(expr)) {
        enterScope();
        for (const auto& p : lambda->params) addSymbol(p, "auto", true, lambda->line, lambda->column);
        analyzeExpression(lambda->body.get());
        exitScope();
    }
    else if (auto comp = dynamic_cast<Comprehension*>(expr)) {
        // Each `for` clause opens a scope that holds its loop variables
        size_t opened = 0;
        for (auto& clause : comp->clauses) {
            analyzeExpression(clause.iterable.get());
            const std::string iter_type = getExpressionType(clause.iterable.get());
            enterScope();
            opened++;
            const std::string elem = clause.var_names.size() == 1 ? elementTypeOf(iter_type) : "auto";
            for (const auto& name : clause.var_names) addSymbol(name, elem, true, clause.line, clause.column);
            for (auto& cond : clause.conditions) analyzeExpression(cond.get());
        }
        analyzeExpression(comp->element.get());
        if (comp->value) analyzeExpression(comp->value.get());

        std::string elem = getExpressionType(comp->element.get());
        if (elem == "str") elem = "std::string";
        if (elem == "unknown" || elem == "void" || elem.find("auto") != std::string::npos) elem = "auto";
        comp->element_type = elem;
        std::string type = "auto";
        if (elem != "auto") {
            if (comp->kind == Comprehension::Kind::List) type = "std::vector<" + elem + ">";
            else if (comp->kind == Comprehension::Kind::Set) type = "std::set<" + elem + ">";
            else {
                std::string v = getExpressionType(comp->value.get());
                if (v == "str") v = "std::string";
                if (v != "unknown" && v != "void" && v.find("auto") == std::string::npos) {
                    type = "std::map<" + elem + "," + v + ">";
                    comp->value_type = v;
                }
            }
        }
        expr_types[comp] = type;
        if (getExpressionType(comp->element.get()) == "void")
            errorAt("E0104", "the comprehension's value has no type: the function returns nothing", comp->element.get());
        while (opened--) exitScope();
    }
    else if (auto tuple_lit = dynamic_cast<TupleLiteral*>(expr)) {
        trackBuiltinUsage("tuple");
        for (auto& element : tuple_lit->elements) analyzeExpression(element.get());
    }
    else if (auto list_lit = dynamic_cast<ListLiteral*>(expr)) {
        trackBuiltinUsage("vector");
        for (auto& element : list_lit->elements) analyzeExpression(element.get());
    }
    else if (auto dict_lit = dynamic_cast<DictLiteral*>(expr)) {
        trackBuiltinUsage("map");
        for (auto& item : dict_lit->items) {
            analyzeExpression(item.first.get());
            analyzeExpression(item.second.get());
        }
    }
    else if (auto bin_op = dynamic_cast<BinaryOp*>(expr)) {
        static const std::unordered_set<std::string> compound_ops = {
            "+=", "-=", "*=", "/=", "//=", "%=", "**=", "&=", "|=", "^=", "<<=", ">>=" };
        if (compound_ops.count(bin_op->op)) {
            analyzeExpression(bin_op->left.get());
            if (!isValidAssignmentTarget(bin_op->left.get())) {
                errorAt("E0008", "invalid assignment target for compound assignment", bin_op->left.get());
            }
            analyzeExpression(bin_op->right.get());

            const std::string left_type = getExpressionType(bin_op->left.get());
            const std::string right_type = getExpressionType(bin_op->right.get());
            if (!typesAreCompatible(left_type, right_type) &&
                !(bin_op->op == "+=" && startsWith(left_type, "std::vector<"))) {
                warningAt("W0005", "mismatched types in '" + bin_op->op + "': " + friendlyType(left_type) +
                          " and " + friendlyType(right_type), bin_op);
            }
            if (bin_op->op == "/=" && isIntegerType(left_type)) {
                warningAt("W0008", "'/=' on an integer truncates the result", bin_op,
                          "'" + friendlyType(left_type) + "' cannot hold a fraction; use '//=' for floor division, "
                          "or make the variable a float");
            }
        }
        else if (bin_op->op == "=") {
            analyzeExpression(bin_op->left.get());
            analyzeExpression(bin_op->right.get());
            const std::string left_type = getExpressionType(bin_op->left.get());
            const std::string right_type = getExpressionType(bin_op->right.get());
            if (right_type == "void") {
                errorAt("E0104", "this value has no type: the function returns nothing", bin_op->right.get());
            }
            if (!typesAreCompatible(left_type, right_type)) {
                errorAt("E0104", "mismatched types: expected " + friendlyType(left_type) + ", found " + friendlyType(right_type),
                        bin_op, "convert explicitly, e.g. int(x), float(x) or str(x)");
            }
            checkIntegerTarget(bin_op->right.get(), left_type, "assigned to an integer");
            if (auto m = dynamic_cast<MemberAccess*>(bin_op->left.get())) {   // self.callback = lambda ...
                if (auto lam = dynamic_cast<Lambda*>(bin_op->right.get())) { (void)m; lam->by_value = true; }
            }
        }
        else {
            analyzeExpression(bin_op->left.get());
            analyzeExpression(bin_op->right.get());

            const std::string left_type = getExpressionType(bin_op->left.get());
            const std::string right_type = getExpressionType(bin_op->right.get());
            if (!typesAreCompatible(left_type, right_type) &&
                bin_op->op != "in" && bin_op->op != "not in" && bin_op->op != "is" && bin_op->op != "is not" &&
                !(bin_op->op == "*" && (isStrType(left_type) || startsWith(left_type, "std::vector<")) && isIntegerType(right_type))) {
                warningAt("W0005", "mismatched types in '" + bin_op->op + "': " + friendlyType(left_type) +
                          " and " + friendlyType(right_type), bin_op);
            }
        }
    }
    else if (auto call = dynamic_cast<CallExpr*>(expr)) {
        if (isSuperCall(call)) {
            checkSuper(call);
            return;
        }
        analyzeExpression(call->callee.get());
        for (auto& arg : call->arguments) analyzeExpression(arg.get());

        bool keyword_args = false;
        for (auto& a : call->arguments)
            if (auto b = dynamic_cast<BinaryOp*>(a.get()); b && b->op == "=") keyword_args = true;

        if (call->is_subscript) {
            const std::string container = getExpressionType(call->callee.get());
            if (startsWith(container, "std::vector<") && call->arguments.size() == 1) {
                const std::string index_type = getExpressionType(call->arguments[0].get());
                checkIntegerTarget(call->arguments[0].get(), "int", "used as a list index");
                if (!isIntegerType(index_type) && index_type != "bool" && index_type != "auto" &&
                    index_type != "unknown" && !isFloatType(index_type)) {
                    warningAt("W0009", "list index should be an integer, found " + friendlyType(index_type),
                              call->arguments[0].get());
                }
            }
            return;
        }

        if (keyword_args) return;   // keyword arguments are matched by the C++ compiler

        if (auto member = dynamic_cast<MemberAccess*>(call->callee.get())) {
            checkMethodCall(call, member);
        }
        else if (auto callee = dynamic_cast<Identifier*>(call->callee.get())) {
            // [fix2] arity and argument types of calls to user-defined functions
            auto fn = functions.find(callee->name);
            Symbol* bound = findSymbol(callee->name);
            if (fn != functions.end() && (!bound || bound->type == "function") && !fn->second.generic) {
                checkCallArguments(call, "function '" + callee->name + "'", fn->second,
                                   static_cast<int>(callee->name.size()));
            }
            else if ((!bound || bound->type == "class" || bound->type == "imported_symbol") &&
                     classes.count(callee->name) && !classes[callee->name].is_struct) {
                // Constructor: Player("Ann", 3) is checked against Player.__init__
                const FunctionSymbol* init = findMethod(callee->name, "__init__");
                if (init) {
                    checkCallArguments(call, "'" + callee->name + "' (its __init__)", *init,
                                       static_cast<int>(callee->name.size()));
                }
                else if (!call->arguments.empty() && classFullyKnown(callee->name)) {
                    error("E0106", "class '" + callee->name + "' has no __init__, so it takes no arguments, but " +
                          std::to_string(call->arguments.size()) + (call->arguments.size() == 1 ? " was" : " were") +
                          " supplied", callee->line, callee->column,
                          "add `def __init__(self, ...):` to the class", static_cast<int>(callee->name.size()));
                }
            }
            else if (callee->name == "range") {
                for (auto& arg : call->arguments) checkIntegerTarget(arg.get(), "int", "passed to range()");
            }
            else if (bound && bound->is_var && (isIntegerType(bound->type) || isFloatType(bound->type) ||
                     isStrType(bound->type) || bound->type == "bool")) {
                errorAt("E0115", "'" + callee->name + "' is a " + friendlyType(bound->type) + ", not a function",
                        callee);
            }
        }

        // Track built-in function calls
        if (auto ident = dynamic_cast<Identifier*>(call->callee.get())) {
            if (ident->name == "print") {
                trackBuiltinUsage("print");
                trackBuiltinUsage("iostream");
            }
            else if (ident->name == "len") trackBuiltinUsage("len");
            else if (ident->name == "mem.alloc" || ident->name == "mem.free") trackBuiltinUsage("cstdlib");
        }
    }
    else if (auto unary_op = dynamic_cast<UnaryOp*>(expr)) {
        analyzeExpression(unary_op->operand.get());
    }
    else if (auto ternary_op = dynamic_cast<TernaryOp*>(expr)) {
        analyzeExpression(ternary_op->condition.get());
        analyzeExpression(ternary_op->then_expr.get());
        analyzeExpression(ternary_op->else_expr.get());
    }
}

// ===== IMPORT HANDLING =====

// `import graphics` and `import graphics.shapes` both bind `graphics`: merge, don't collide.
void SemanticAnalyzer::bindModuleName(const std::string& name, ImportStmt* import) {
    auto& scope = scopes.back();
    auto it = scope.find(name);
    if (it != scope.end()) {
        if (it->second.type == "module") return;   // same namespace, nothing to add
        error("E0103", "'" + name + "' conflicts with an existing declaration", import->line, import->column,
              "use `import ... as <other name>` to bind the module under a different name");
    }
    addSymbol(name, "module", true);
}

void SemanticAnalyzer::analyzeImportStmt(ImportStmt* import) {
    pycpp::debugLog() << "Processing import: " << import->module << std::endl;

    // `import a.b.c` binds `a`; `import a.b.c as x` binds `x`; `from a.b import n` binds only n.
    if (!import->is_from) {
        const std::string bound = !import->alias.empty()
            ? import->alias
            : import->module.substr(0, import->module.find('.'));
        bindModuleName(bound, import);
    }

    for (const auto& item : import->imports) {
        auto& scope = scopes.back();
        auto it = scope.find(item);
        if (it != scope.end() && (it->second.type == "imported_symbol" || it->second.type == "function")) continue;  // imported twice
        // A function whose signature is known (its module was analysed first) can be checked
        const bool known_function = functions.count(item) && !import->header_stem.empty() &&
            exportedFunctions().count(import->header_stem + "::" + item);
        addSymbol(item, known_function ? "function" : "imported_symbol", true);
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
            func_sym.generic = !func->type_params.empty();   // [fix2]

            for (const auto& param : func->parameters) {
                func_sym.parameters.emplace_back(param.name, typeToString(param.type.get()));
                if (!param.default_value) func_sym.required = func_sym.parameters.size();
            }

            functions[func->name] = func_sym;

            // === FIX: Add function to global symbol table ===
            addSymbol(func->name, "function", true, func->line, func->column + 4);
            pycpp::debugLog() << "Registered and added to symbol table function: " << func->name << " -> " << func_sym.return_type << std::endl;
        }
        else if (auto cls = dynamic_cast<ClassDef*>(stmt.get())) {
            ClassSymbol cls_sym;
            cls_sym.name = cls->name;
            cls_sym.base_class = cls->base_class;
            classes[cls->name] = cls_sym;

            // === FIX: Add class to global symbol table ===
            addSymbol(cls->name, "class", true, cls->line, cls->column + 6);
            pycpp::debugLog() << "Registered and added to symbol table class: " << cls->name << std::endl;
        }
        else if (auto struc = dynamic_cast<StructDef*>(stmt.get())) {
            ClassSymbol cls_sym;
            cls_sym.name = struc->name;
            classes[struc->name] = cls_sym;

            // === FIX: Add struct to global symbol table ===
            addSymbol(struc->name, "class", true, struc->line, struc->column + 7);
            pycpp::debugLog() << "Registered and added to symbol table struct: " << struc->name << std::endl;
        }
        else if (auto import = dynamic_cast<ImportStmt*>(stmt.get())) {
            // Classes and functions that an already analysed module exports
            if (import->is_from && !import->header_stem.empty()) {
                for (const auto& item : import->imports) {
                    const std::string key = import->header_stem + "::" + item;
                    auto c = exportedClasses().find(key);
                    if (c != exportedClasses().end() && !classes.count(item)) classes[item] = c->second;
                    auto f = exportedFunctions().find(key);
                    if (f != exportedFunctions().end() && !functions.count(item)) functions[item] = f->second;
                }
            }
        }
        else if (auto use = dynamic_cast<UseStmt*>(stmt.get())) {
            has_cpp_headers = true;
            pycpp::debugLog() << "Found use statement: " << use->library << std::endl;
        }
    }

    // Method signatures and members of every class, before any body is analysed
    for (auto& stmt : program->statements)
        if (auto cls = dynamic_cast<ClassDef*>(stmt.get())) registerClassMembers(cls);

    // Second pass: analyze statements (errors are collected)
    for (auto& stmt : program->statements) {
        analyzeGuarded(stmt.get());
    }

    // What this module offers to modules that import it
    if (!module_stem.empty()) {
        for (auto& stmt : program->statements) {
            std::string name;
            if (auto f = dynamic_cast<FunctionDef*>(stmt.get())) {
                if (functions.count(f->name)) exportedFunctions()[module_stem + "::" + f->name] = functions[f->name];
                continue;
            }
            if (auto c = dynamic_cast<ClassDef*>(stmt.get())) name = c->name;
            else if (auto st = dynamic_cast<StructDef*>(stmt.get())) name = st->name;
            if (!name.empty() && classes.count(name)) exportedClasses()[module_stem + "::" + name] = classes[name];
        }
    }
    if (!collected.empty()) {
        std::vector<pycpp::Diagnostic> all = std::move(collected);
        collected.clear();
        throw pycpp::CompileError(std::move(all));
    }
}

void SemanticAnalyzer::checkUnreachable(const std::vector<std::unique_ptr<Stmt>>& body) {
    // [fix2] code after return/break/continue/raise in the same block never runs
    for (size_t i = 0; i < body.size(); ++i) {
        Stmt* s = body[i].get();
        if (auto f = dynamic_cast<IfStmt*>(s)) {
            checkUnreachable(f->then_branch);
            for (auto& e : f->elif_branches) checkUnreachable(e.second);
            checkUnreachable(f->else_branch);
        }
        else if (auto w = dynamic_cast<WhileStmt*>(s)) checkUnreachable(w->body);
        else if (auto fo = dynamic_cast<ForStmt*>(s)) checkUnreachable(fo->body);
        else if (auto u = dynamic_cast<UnsafeBlock*>(s)) checkUnreachable(u->body);
        else if (auto d = dynamic_cast<DeferStmt*>(s)) checkUnreachable(d->body);
        else if (auto t = dynamic_cast<TryStmt*>(s)) {
            checkUnreachable(t->body);
            for (auto& h : t->handlers) checkUnreachable(h.body);
            checkUnreachable(t->else_body);
            checkUnreachable(t->finally_body);
        }
        else if (auto m = dynamic_cast<MatchStmt*>(s)) {
            for (auto& c : m->cases) checkUnreachable(c.body);
        }

        const char* what = nullptr;
        if (dynamic_cast<ReturnStmt*>(s)) what = "return";
        else if (dynamic_cast<BreakStmt*>(s)) what = "break";
        else if (dynamic_cast<ContinueStmt*>(s)) what = "continue";
        else if (dynamic_cast<RaiseStmt*>(s)) what = "raise";
        if (what && i + 1 < body.size()) {
            Stmt* next = body[i + 1].get();
            int ln = next->line, col = next->column, len = 1;
            if (auto es = dynamic_cast<ExprStmt*>(next); es && es->expr) {
                const SourceSpan sp = spanOf(es->expr.get());
                ln = sp.line; col = sp.column; len = sp.length;
            }
            warning("W0002", "unreachable code", ln, col,
                    std::string("nothing after this `") + what + "` in the same block can run; remove it or move it before the `" + what + "`", len);
            return;
        }
    }
}

void SemanticAnalyzer::analyzeGuarded(Stmt* stmt) {
    // [diag-fix] Record the error and continue, so one run reports as many problems as possible.
    const size_t depth = scopes.size();
    const int scope_level = current_scope;
    FunctionSymbol* fn = current_function;
    ClassSymbol* cls = current_class;
    const int handlers = handler_depth;
    try {
        analyzeStatement(stmt);
    }
    catch (const pycpp::CompileError& e) {
        for (const auto& d : e.diagnostics) collected.push_back(d);
        while (scopes.size() > depth) scopes.pop_back();
        current_scope = scope_level;
        current_function = fn;
        current_class = cls;
        handler_depth = handlers;
        // declare the variable anyway, so later uses do not produce a cascade of errors
        // (with the type it was given, or the type of its value, so later checks still work)
        if (auto v = dynamic_cast<VarDecl*>(stmt)) {
            if (v->var_name.find("self.") != 0 && !symbolExistsInCurrentScope(v->var_name)) {
                std::string type = v->type ? typeToString(v->type.get()) : getExpressionType(v->value.get());
                if (type == "unknown" || type == "void" || type == "None" || type.find("auto") != std::string::npos)
                    type = "auto";
                scopes.back()[v->var_name] = Symbol{ v->var_name, type, true, current_scope };
            }
        }
        else if (auto u = dynamic_cast<UnpackStmt*>(stmt)) {   // keep the names, avoid follow-up errors
            for (auto& target : u->targets)
                if (auto id = dynamic_cast<Identifier*>(target.get()))
                    if (!findSymbol(id->name) && !symbolExistsInCurrentScope(id->name))
                        scopes.back()[id->name] = Symbol{ id->name, "auto", true, current_scope };
        }
    }
}

void SemanticAnalyzer::analyzeStatement(Stmt* stmt) {
    if (!current_function) {
        const char* kind = nullptr;
        if (dynamic_cast<ExprStmt*>(stmt)) kind = "expression statements";
        else if (dynamic_cast<IfStmt*>(stmt)) kind = "'if' statements";
        else if (dynamic_cast<WhileStmt*>(stmt)) kind = "'while' loops";
        else if (dynamic_cast<ForStmt*>(stmt)) kind = "'for' loops";
        else if (dynamic_cast<UnpackStmt*>(stmt)) kind = "unpacking assignments";
        if (kind) {
            int ln = stmt->line, col = stmt->column, len = 1;
            if (auto es = dynamic_cast<ExprStmt*>(stmt); es && es->expr) {
                const SourceSpan sp = spanOf(es->expr.get());
                ln = sp.line; col = sp.column; len = sp.length;
            }
            error("E0108", std::string(kind) + " are only allowed inside a function", ln, col,
                  "put this code inside `def main() -> int:`", len);
        }
    }
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
    else if (auto defer_stmt = dynamic_cast<DeferStmt*>(stmt)) {
        analyzeDeferStmt(defer_stmt);
    }
    else if (auto unpack_stmt = dynamic_cast<UnpackStmt*>(stmt)) {
        analyzeUnpack(unpack_stmt);
    }
    else if (auto try_stmt = dynamic_cast<TryStmt*>(stmt)) {
        analyzeTryStmt(try_stmt);
    }
    else if (auto raise_stmt = dynamic_cast<RaiseStmt*>(stmt)) {
        analyzeRaiseStmt(raise_stmt);
    }
    else if (auto match_stmt = dynamic_cast<MatchStmt*>(stmt)) {
        analyzeMatchStmt(match_stmt);
    }
    else if (auto unsafe_block = dynamic_cast<UnsafeBlock*>(stmt)) {
        enterScope();
        analyzeBody(unsafe_block->body);
        exitScope();
    }
    else if (auto block = dynamic_cast<Block*>(stmt)) {
        enterScope();
        analyzeBody(block->statements);
        exitScope();
    }
}

// In SemanticAnalyzer::analyzeVarDecl - MODIFY the member variable handling:
void SemanticAnalyzer::analyzeVarDecl(VarDecl* var) {
    if (var->inferred && !var->type) {
        // `x = value`: x gets the type of value
        analyzeExpression(var->value.get());
        const std::string t = getExpressionType(var->value.get());
        const std::string how = "declare its type: `" + var->var_name + ": list[int] = []`";
        if (t == "void")
            errorAt("E0114", "cannot infer the type of '" + var->var_name + "': the value is a call that returns nothing",
                    var->value.get());
        if (t == "None")
            errorAt("E0114", "cannot infer the type of '" + var->var_name + "' from None", var->value.get(),
                    "declare it as a pointer: `" + var->var_name + ": *Node = None`");
        bool empty_call = false;
        if (auto call = dynamic_cast<CallExpr*>(var->value.get()); call && call->arguments.empty())
            if (auto id = dynamic_cast<Identifier*>(call->callee.get()))
                empty_call = id->name == "list" || id->name == "dict" || id->name == "set";
        if (empty_call || t.find("auto>") != std::string::npos || t.find("auto,") != std::string::npos)
            errorAt("E0114", "cannot infer the type of '" + var->var_name + "' from an empty or untyped collection",
                    var->value.get(), how);
        var->type = typeFromString(t == "float" ? "float" : t);
        var->type->line = var->line;
        var->type->column = var->column;
        if (!var->value) return;
    }
    std::string type_name = typeToString(var->type.get());

    if (!isValidType(type_name) && !has_cpp_headers) {
        {
            std::vector<std::string> known;
            for (const auto& kv : classes) known.push_back(kv.first);
            const std::string guess = pycpp::closestMatch(type_name, known);
            error("E0102", "cannot find type '" + friendlyType(type_name) + "' in this scope",
                var->type ? var->type->line : var->line, var->type ? var->type->column : var->column,
                guess.empty() ? std::string("declare the class/struct first, or import it with `from module import Name`")
                              : "a similar type exists, did you mean '" + guess + "'?",
                static_cast<int>(friendlyType(type_name).size()));
        }
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
                    error("E0103", "the name '" + var->var_name + "' is defined more than once in this scope", var->line, var->column,
                        "rename one of them, or remove the duplicate declaration", static_cast<int>(var->var_name.size()));
                    return;
                }
            }
            addSymbol(var->var_name, type_name, var->value != nullptr, var->line, var->column);
            scopes.back()[var->var_name].is_var = (current_function != nullptr);   // [fix2]
        }
    }

    if (var->value && !var->inferred) {
        analyzeExpression(var->value.get());
        std::string expr_type = getExpressionType(var->value.get());
        if (auto lit = dynamic_cast<IntLiteral*>(var->value.get())) {   // 3000000000 into an int
            const long long lo = type_name == "int8_t" ? -128 : type_name == "int16_t" ? -32768 : -2147483648LL;
            const long long hi = type_name == "int8_t" ? 127 : type_name == "int16_t" ? 32767 : 2147483647LL;
            if ((type_name == "int" || type_name == "int32_t" || type_name == "int8_t" || type_name == "int16_t") &&
                (lit->value < lo || lit->value > hi))
                errorAt("E0010", "integer literal " + std::to_string(lit->value) + " does not fit in '" +
                        friendlyType(type_name) + "' (" + std::to_string(lo) + " to " + std::to_string(hi) + ")",
                        lit, "declare the variable as i64");
        }
        if (expr_type == "void")
            errorAt("E0104", "'" + var->var_name + "' cannot be initialised with a call that returns nothing",
                    var->value.get());
        // `n: int = input(...)` converts the entered text to the declared type
        bool converted_input = false;
        if (auto call = dynamic_cast<CallExpr*>(var->value.get()))
            if (auto id = dynamic_cast<Identifier*>(call->callee.get()); id && id->name == "input" && !call->is_subscript)
                converted_input = isIntegerType(type_name) || isFloatType(type_name) || type_name == "bool";
        if (!converted_input && !typesAreCompatible(type_name, expr_type)) {
            error("E0104", "mismatched types: '" + var->var_name + "' is " + friendlyType(type_name) +
                ", but the value is " + friendlyType(expr_type), var->line, var->column,
                "convert explicitly, e.g. int(x), float(x) or str(x)");
        }
    }

    // [fix2] a float value stored in an integer variable silently loses its fractional part
    if (var->value) checkIntegerTarget(var->value.get(), type_name, "stored in '" + var->var_name + "' (" + friendlyType(type_name) + ")");

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
        addSymbol(param.name, param_type, true, func->line, func->column);
        pycpp::debugLog() << "Added parameter to function scope: " << param.name << " : " << param_type << std::endl;
    }

    ended_names.clear();
    checkUnreachable(func->body);   // [fix2]

    // super().__init__(...) must open __init__: C++ builds the base before the derived class
    std::function<void(std::vector<std::unique_ptr<Stmt>>&, bool)> findSuperInit =
        [&](std::vector<std::unique_ptr<Stmt>>& body, bool top) {
        for (size_t i = 0; i < body.size(); ++i) {
            Stmt* st = body[i].get();
            if (auto es = dynamic_cast<ExprStmt*>(st)) {
                auto call = dynamic_cast<CallExpr*>(es->expr.get());
                auto m = call ? dynamic_cast<MemberAccess*>(call->callee.get()) : nullptr;
                if (m && m->member == "__init__" && isSuperCall(m->object.get())) {
                    if (func->name != "__init__")
                        errorAt("E0113", "super().__init__() can only be called from __init__", call);
                    if (!top || i != 0)
                        errorAt("E0113", "super().__init__() must be the first statement of __init__", call,
                                "C++ constructs the base class before the derived one; move this call to the top");
                    for (auto& arg : call->arguments) {
                        std::function<bool(Expr*)> usesSelf = [&](Expr* e) -> bool {
                            if (!e) return false;
                            if (auto id = dynamic_cast<Identifier*>(e)) return id->name == "self";
                            if (auto b = dynamic_cast<BinaryOp*>(e)) return usesSelf(b->left.get()) || usesSelf(b->right.get());
                            if (auto u = dynamic_cast<UnaryOp*>(e)) return usesSelf(u->operand.get());
                            if (auto ma = dynamic_cast<MemberAccess*>(e)) return usesSelf(ma->object.get());
                            if (auto t = dynamic_cast<TernaryOp*>(e))
                                return usesSelf(t->condition.get()) || usesSelf(t->then_expr.get()) || usesSelf(t->else_expr.get());
                            if (auto c = dynamic_cast<CallExpr*>(e)) {
                                if (usesSelf(c->callee.get())) return true;
                                for (auto& a : c->arguments) if (usesSelf(a.get())) return true;
                            }
                            return false;
                        };
                        if (usesSelf(arg.get()))
                            errorAt("E0113", "the arguments of super().__init__() cannot use 'self'", arg.get(),
                                    "the object does not exist yet while its base class is being constructed");
                    }
                }
            }
            else if (auto f = dynamic_cast<IfStmt*>(st)) {
                findSuperInit(f->then_branch, false);
                for (auto& e : f->elif_branches) findSuperInit(e.second, false);
                findSuperInit(f->else_branch, false);
            }
            else if (auto w = dynamic_cast<WhileStmt*>(st)) findSuperInit(w->body, false);
            else if (auto fo = dynamic_cast<ForStmt*>(st)) findSuperInit(fo->body, false);
            else if (auto t = dynamic_cast<TryStmt*>(st)) findSuperInit(t->body, false);
        }
    };
    if (current_class) findSuperInit(func->body, true);

    analyzeBody(func->body, true);

    // Every path through a function with a return type must return (or raise)
    const std::string declared_return = typeToString(func->return_type.get());
    if (declared_return != "void" && declared_return != "None" && declared_return != "auto" &&
        func->name != "__init__" && !(func->name == "main" && !current_class) && !terminates(func->body)) {
        const int name_column = func->column + 4;   // after "def "
        if (!current_function->has_return) {
            warning("W0003", "function '" + func->name + "' is declared to return " + friendlyType(declared_return) +
                    " but contains no 'return'", func->line, name_column,
                    "add `return <value>`, or change the return type to `void`", static_cast<int>(func->name.size()));
        }
        else {
            pycpp::Diagnostic d;
            d.severity = pycpp::Severity::Warning;
            d.code = "W0003";
            d.message = "not every path through '" + func->name + "' returns a value";
            d.line = func->line;
            d.column = name_column;
            d.length = static_cast<int>(func->name.size());
            d.label = "can end without returning " + friendlyType(declared_return);
            if (!func->body.empty()) {
                const Stmt* last = func->body.back().get();
                d.notes.push_back("execution can reach the end of the function after line " +
                                  std::to_string(last->line) + ", where nothing is returned");
            }
            d.hint = "add a final `return <value>`, or make sure every branch returns (an `if` needs an `else`)";
            pycpp::emit(d);
        }
    }

    exitScope(); // Exit function scope
    current_function = nullptr;
}

void SemanticAnalyzer::analyzeClass(ClassDef* cls) {
    pycpp::debugLog() << "Analyzing class: " << cls->name << std::endl;

    current_class = &classes[cls->name];

    // Method signatures and constructor members (also done before the second pass)
    registerClassMembers(cls);

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

    // Allow pointer dereference (*p = value)
    if (auto unary = dynamic_cast<UnaryOp*>(expr)) {
        return unary->op == "*";
    }

    // Allow array subscript (arr[index])
    if (auto call = dynamic_cast<CallExpr*>(expr)) {
        if (call->is_subscript) {
            return true; // a[i], obj.items[i], a[i][j]   [fix2]
        }
    }

    return false;
}

// FIXED: Enhanced scope management for if statements
void SemanticAnalyzer::analyzeIfStmt(IfStmt* if_stmt) {
    analyzeExpression(if_stmt->condition.get());

    enterScope();
    analyzeBody(if_stmt->then_branch);
    exitScope();

    for (auto& elif_pair : if_stmt->elif_branches) {
        analyzeExpression(elif_pair.first.get());
        enterScope();
        analyzeBody(elif_pair.second);
        exitScope();
    }

    if (!if_stmt->else_branch.empty()) {
        enterScope();
        analyzeBody(if_stmt->else_branch);
        exitScope();
    }
}

void SemanticAnalyzer::analyzeWhileStmt(WhileStmt* while_stmt) {
    analyzeExpression(while_stmt->condition.get());
    enterScope();
    analyzeBody(while_stmt->body);
    exitScope();
}

void SemanticAnalyzer::analyzeForStmt(ForStmt* for_stmt) {
    analyzeExpression(for_stmt->iterable.get());
    // The loop variable has the iterable's element type when it is known
    // (for p in players: p.update() is then checked like any other call).
    const std::string iter_type = getExpressionType(for_stmt->iterable.get());
    enterScope();
    if (for_stmt->var_names.size() <= 1) {
        addSymbol(for_stmt->var_name, elementTypeOf(iter_type), true);
    }
    else {
        for (const auto& name : for_stmt->var_names) addSymbol(name, "auto", true);
    }
    analyzeBody(for_stmt->body);
    exitScope();
}

void SemanticAnalyzer::analyzeReturnStmt(ReturnStmt* return_stmt) {
    if (!current_function) {
        error("E0109", "return statement outside function", return_stmt->line, return_stmt->column);
    }
    current_function->has_return = true;
    if (!return_stmt->value) return;
    if (auto lam = dynamic_cast<Lambda*>(return_stmt->value.get())) lam->by_value = true;   // it outlives this call
    analyzeExpression(return_stmt->value.get());

    const std::string return_type = getExpressionType(return_stmt->value.get());
    if (return_type == "void" && current_function->return_type != "void") {
        errorAt("E0105", "this returns the result of a call that returns nothing", return_stmt->value.get());
    }
    if (!typesAreCompatible(current_function->return_type, return_type)) {
        errorAt("E0105", "mismatched return type: function returns " + friendlyType(current_function->return_type) +
                " but this returns " + friendlyType(return_type), return_stmt->value.get(),
                "convert explicitly, or change the return type after '->'");
    }
    checkIntegerTarget(return_stmt->value.get(), current_function->return_type,
                       "returned from a function that returns " + friendlyType(current_function->return_type));
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

void SemanticAnalyzer::error(const char* code, const std::string& message, int line, int column, const std::string& hint, int length) {
    pycpp::Diagnostic d;
    d.code = code;
    d.message = message; d.line = line; d.column = column; d.hint = hint; d.length = length;
    throw pycpp::CompileError(d);   // rendered once, by the caller
}

void SemanticAnalyzer::warning(const char* code, const std::string& message, int line, int column, const std::string& hint, int length) {
    pycpp::Diagnostic d;
    d.severity = pycpp::Severity::Warning;
    d.code = code;
    d.message = message; d.line = line; d.column = column; d.hint = hint; d.length = length;
    pycpp::emit(d);
}

// The same, pointing at the whole source text of `node`.
void SemanticAnalyzer::errorAt(const char* code, const std::string& message, const ASTNode* node, const std::string& hint) {
    const SourceSpan s = spanOf(node);
    error(code, message, s.line, s.column, hint, s.length);
}

void SemanticAnalyzer::warningAt(const char* code, const std::string& message, const ASTNode* node, const std::string& hint) {
    const SourceSpan s = spanOf(node);
    warning(code, message, s.line, s.column, hint, s.length);
}

void SemanticAnalyzer::analyzeDeferStmt(DeferStmt* d) {
    if (!current_function)
        error("E0110", "'defer' can only be used inside a function", d->line, d->column,
              "defer schedules cleanup for when the enclosing scope exits");
    for (auto& s : d->body) {
        if (dynamic_cast<ReturnStmt*>(s.get()) || dynamic_cast<BreakStmt*>(s.get()) ||
            dynamic_cast<ContinueStmt*>(s.get()))
            error("E0110", "'return', 'break' and 'continue' are not allowed inside a defer block", s->line, s->column);
    }
    enterScope();
    analyzeBody(d->body);
    exitScope();
}

void SemanticAnalyzer::analyzeMatchStmt(MatchStmt* m) {
    analyzeExpression(m->subject.get());
    const std::string subject_type = getExpressionType(m->subject.get());
    std::unordered_set<std::string> seen;
    bool saw_default = false;
    for (auto& c : m->cases) {
        if (saw_default)
            error("E0111", "this case can never match because 'case _' already catches everything", c.line, c.column,
                  "move 'case _' to the end of the match block");
        if (c.is_default) saw_default = true;
        for (auto& p : c.patterns) {
            analyzeExpression(p.get());
            const std::string pt = getExpressionType(p.get());
            if (!typesAreCompatible(subject_type, pt))
                error("E0111", "cannot match a value of type " + friendlyType(subject_type) +
                      " against a " + friendlyType(pt) + " pattern", p->line, p->column);
            std::string key;
            if (auto i = dynamic_cast<IntLiteral*>(p.get())) key = "i" + std::to_string(i->value);
            else if (auto s = dynamic_cast<StringLiteral*>(p.get())) key = "s" + s->value;
            if (!key.empty() && !seen.insert(key).second)
                error("E0111", "duplicate case pattern", p->line, p->column, "each value can appear in only one case");
        }
        enterScope();
        analyzeBody(c.body);
        exitScope();
    }
}

void SemanticAnalyzer::analyzeTryStmt(TryStmt* t) {
    if (!current_function)
        error("E0110", "'try' can only be used inside a function", t->line, t->column,
              "put the try statement inside a def");

    enterScope();
    analyzeBody(t->body);
    exitScope();

    for (auto& h : t->handlers) {
        for (const auto& name : h.type_names) {
            if (!isBuiltinException(name) && !classes.count(name) && !findSymbol(name))
                warning("W0007", "unknown exception type '" + name + "'", h.line, h.column,
                        "use a built-in one (Exception, ValueError, ZeroDivisionError, ...) "
                        "or define it with: class " + name + "(Exception)");
        }
        enterScope();
        if (!h.var_name.empty())
            addSymbol(h.var_name, h.type_names.size() == 1 ? h.type_names[0] : std::string("Exception"), true);
        handler_depth++;
        analyzeBody(h.body);
        handler_depth--;
        exitScope();
    }

    if (!t->else_body.empty()) {
        enterScope();
        analyzeBody(t->else_body);
        exitScope();
    }

    for (auto& s : t->finally_body) {
        if (dynamic_cast<ReturnStmt*>(s.get()) || dynamic_cast<BreakStmt*>(s.get()) ||
            dynamic_cast<ContinueStmt*>(s.get()))
            error("E0110", "'return', 'break' and 'continue' are not allowed directly inside a finally block",
                  s->line, s->column);
    }
    enterScope();
    analyzeBody(t->finally_body);
    exitScope();
}

void SemanticAnalyzer::analyzeRaiseStmt(RaiseStmt* r) {
    if (!current_function)
        error("E0110", "'raise' can only be used inside a function", r->line, r->column);
    if (!r->value) {
        if (handler_depth == 0)
            error("E0110", "'raise' with no exception to re-raise", r->line, r->column,
                  "use `raise ValueError(\"message\")`, or put a bare `raise` inside an except block");
        return;
    }
    analyzeExpression(r->value.get());
}

// a, b = value: every target is assigned from the tuple, or declared when it is a new name
void SemanticAnalyzer::analyzeUnpack(UnpackStmt* u) {
    analyzeExpression(u->value.get());
    const std::string value_type = getExpressionType(u->value.get());
    const size_t count = u->targets.size();

    std::vector<std::string> parts;
    if (startsWith(value_type, "std::tuple<") && value_type.back() == '>') {
        parts = splitTypeArgs(value_type);
        if (parts.size() != count) {
            errorAt("E0104", "cannot unpack " + std::to_string(parts.size()) + (parts.size() == 1 ? " value" : " values") +
                    " into " + std::to_string(count) + " names", u->value.get(),
                    "the number of names on the left must match the number of values in the tuple");
        }
    }
    else if (startsWith(value_type, "std::vector<") || startsWith(value_type, "std::map<") ||
             startsWith(value_type, "std::set<") || isStrType(value_type) || isIntegerType(value_type) ||
             isFloatType(value_type) || value_type == "bool") {
        errorAt("E0104", "cannot unpack a value of type " + friendlyType(value_type), u->value.get(),
                "only tuples can be unpacked: `a, b = (1, 2)`");
    }

    u->is_new.assign(count, false);
    u->new_types.assign(count, "");
    for (size_t i = 0; i < count; ++i) {
        Expr* target = u->targets[i].get();
        const std::string element = i < parts.size() ? parts[i] : std::string("auto");
        auto id = dynamic_cast<Identifier*>(target);
        const bool fresh = id && id->name != "self" && id->name.find("::") == std::string::npos &&
                           !findSymbol(id->name) && !functions.count(id->name) && !classes.count(id->name) &&
                           !(current_class && findAttribute(current_class->name, id->name));
        if (fresh) {
            u->is_new[i] = true;
            u->new_types[i] = element;
            addSymbol(id->name, element, true, id->line, id->column);
            scopes.back()[id->name].is_var = (current_function != nullptr);
            continue;
        }
        analyzeExpression(target);
        const std::string target_type = getExpressionType(target);
        if (!typesAreCompatible(target_type, element))
            errorAt("E0104", "mismatched types: expected " + friendlyType(target_type) + ", found " + friendlyType(element),
                    target, "convert explicitly, e.g. int(x), float(x) or str(x)");
    }
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
// ===== Classes, calls and inference =====

std::unordered_map<std::string, SemanticAnalyzer::ClassSymbol>& SemanticAnalyzer::exportedClasses() {
    static std::unordered_map<std::string, ClassSymbol> exported;
    return exported;
}
std::unordered_map<std::string, SemanticAnalyzer::FunctionSymbol>& SemanticAnalyzer::exportedFunctions() {
    static std::unordered_map<std::string, FunctionSymbol> exported;
    return exported;
}
void SemanticAnalyzer::resetModuleExports() {
    exportedClasses().clear();
    exportedFunctions().clear();
}

// Method signatures and member types of a class, known before any body is analysed so
// calls can be checked wherever they appear in the file.
void SemanticAnalyzer::registerClassMembers(ClassDef* cls) {
    ClassSymbol& sym = classes[cls->name];
    for (auto& method : cls->methods) {
        FunctionSymbol m;
        m.name = method->name;
        m.return_type = typeToString(method->return_type.get());
        m.has_return = false;
        m.generic = !method->type_params.empty();
        m.line = method->line;
        for (const auto& param : method->parameters) {
            if (param.name == "self") continue;
            m.parameters.emplace_back(param.name, typeToString(param.type.get()));
            if (!param.default_value) m.required = m.parameters.size();
        }
        sym.methods[method->name] = m;
    }
    for (auto& attr : cls->attributes) {
        std::string n = attr->var_name;
        if (n.rfind("self.", 0) == 0) n = n.substr(5);
        if (!sym.attributes.count(n))
            sym.attributes[n] = { n, typeToString(attr->type.get()), attr->value != nullptr, 0 };
    }
    for (auto& method : cls->methods)
        if (method->name == "__init__") extractMembersFromConstructor(method.get(), cls);
}

bool SemanticAnalyzer::isSubclassOf(const std::string& derived, const std::string& base) {
    std::string current = derived;
    for (int depth = 0; depth < 64; ++depth) {
        auto it = classes.find(current);
        if (it == classes.end() || it->second.base_class.empty()) return false;
        current = it->second.base_class;
        if (current == base) return true;
    }
    return false;
}

const SemanticAnalyzer::ClassSymbol* SemanticAnalyzer::classOf(const std::string& type) {
    auto it = classes.find(type);
    return it == classes.end() ? nullptr : &it->second;
}

const SemanticAnalyzer::FunctionSymbol* SemanticAnalyzer::findMethod(const std::string& cls, const std::string& name) {
    std::string current = cls;
    for (int depth = 0; depth < 64 && !current.empty(); ++depth) {
        const ClassSymbol* c = classOf(current);
        if (!c) return nullptr;
        auto m = c->methods.find(name);
        if (m != c->methods.end()) return &m->second;
        current = c->base_class;
    }
    return nullptr;
}

const SemanticAnalyzer::Symbol* SemanticAnalyzer::findAttribute(const std::string& cls, const std::string& name) {
    std::string current = cls;
    for (int depth = 0; depth < 64 && !current.empty(); ++depth) {
        const ClassSymbol* c = classOf(current);
        if (!c) return nullptr;
        auto a = c->attributes.find(name);
        if (a != c->attributes.end()) return &a->second;
        current = c->base_class;
    }
    return nullptr;
}

// The class and all of its bases are PyCPP classes we have seen, so "no such member"
// is certain (a base from a C++ header or a built-in exception has members we don't know).
bool SemanticAnalyzer::classFullyKnown(const std::string& cls) {
    std::string current = cls;
    for (int depth = 0; depth < 64; ++depth) {
        const ClassSymbol* c = classOf(current);
        if (!c) return false;
        if (c->base_class.empty()) return true;
        current = c->base_class;
    }
    return false;
}

// Type of the items a `for` loop over a value of this type produces.
std::string SemanticAnalyzer::elementTypeOf(const std::string& container) {
    if (startsWith(container, "std::vector<") && container.back() == '>') return container.substr(12, container.size() - 13);
    if (startsWith(container, "std::set<") && container.back() == '>') return container.substr(9, container.size() - 10);
    if (startsWith(container, "std::map<") && container.back() == '>') return mapTypes(container).first;
    if (isStrType(container)) return "char";
    return "auto";
}

// Type string (as produced by getExpressionType) -> type node for the generated declaration.
std::unique_ptr<Type> SemanticAnalyzer::typeFromString(const std::string& t) {
    if (!t.empty() && t.back() == '*') {
        auto ptr = std::make_unique<PointerType>();
        ptr->base_type = typeFromString(t.substr(0, t.size() - 1));
        ptr->name = "*" + ptr->base_type->name;
        return ptr;
    }
    if (startsWith(t, "std::vector<") && t.back() == '>') {
        auto list = std::make_unique<ListType>();
        list->element_type = typeFromString(t.substr(12, t.size() - 13));
        list->name = "std::vector<" + list->element_type->name + ">";
        return list;
    }
    if (startsWith(t, "std::map<") && t.back() == '>') {
        const auto kv = mapTypes(t);
        auto dict = std::make_unique<DictType>();
        dict->key_type = typeFromString(kv.first);
        dict->value_type = typeFromString(kv.second);
        dict->name = "std::map<" + dict->key_type->name + "," + dict->value_type->name + ">";
        return dict;
    }
    if (startsWith(t, "std::tuple<") && t.back() == '>') {
        auto tup = std::make_unique<BasicType>();
        tup->name = "std::tuple";
        for (const auto& part : splitTypeArgs(t)) tup->type_args.push_back(typeFromString(part));
        return tup;
    }
    auto basic = std::make_unique<BasicType>();
    basic->name = (t.empty() || t == "unknown") ? "auto" : isStrType(t) ? "std::string" : t;
    return basic;
}

// Is the type precise enough that a mismatch is certainly an error?
static bool isSimpleType(const std::string& t) {
    static const std::unordered_set<std::string> simple = {
        "int", "float", "double", "bool", "char", "str", "string", "std::string",
        "int8_t", "int16_t", "int32_t", "int64_t" };
    return simple.count(t) > 0;
}

void SemanticAnalyzer::checkCallArguments(CallExpr* call, const std::string& what, const FunctionSymbol& fs,
                                          int name_length) {
    const size_t given = call->arguments.size();
    const size_t total = fs.parameters.size();
    auto plural = [](size_t n) { return std::to_string(n) + (n == 1 ? " argument" : " arguments"); };
    std::string signature = fs.name + "(";
    for (size_t i = 0; i < total; ++i)
        signature += (i ? ", " : "") + fs.parameters[i].first + ": " + friendlyType(fs.parameters[i].second);
    signature += ")";
    if (fs.name != "__init__") signature += " -> " + friendlyType(fs.return_type);
    // "function 'add'" -> "'add'" in the per-argument message
    std::string short_what = what;
    if (short_what.rfind("function ", 0) == 0) short_what = short_what.substr(9);

    if (given < fs.required || given > total) {
        const std::string expected = fs.required == total ? plural(total)
            : std::to_string(fs.required) + " to " + plural(total);
        error("E0106", what + " takes " + expected + " but " + std::to_string(given) +
              (given == 1 ? " was" : " were") + " supplied", call->callee->line, call->callee->column,
              "signature: " + signature, name_length);
    }
    for (size_t i = 0; i < given && i < total; ++i) {
        const std::string& expected = fs.parameters[i].second;
        Expr* arg = call->arguments[i].get();
        const std::string found = getExpressionType(arg);
        const bool expected_known = isSimpleType(expected) || classes.count(expected) > 0;
        const bool found_known = isSimpleType(found) || classes.count(found) > 0;
        if (expected_known && found_known && !typesAreCompatible(expected, found))
            errorAt("E0107", "mismatched types: argument " + std::to_string(i + 1) + " of " + short_what +
                    " expects " + friendlyType(expected) + ", found " + friendlyType(found),
                    arg, "signature: " + signature);
        checkIntegerTarget(arg, expected, "passed to the " + friendlyType(expected) + " parameter '" +
                           fs.parameters[i].first + "'");
    }
}

bool SemanticAnalyzer::isSuperCall(Expr* expr) {
    auto call = dynamic_cast<CallExpr*>(expr);
    if (!call || call->is_subscript) return false;
    auto id = dynamic_cast<Identifier*>(call->callee.get());
    return id && id->name == "super" && !findSymbol("super");
}

void SemanticAnalyzer::checkSuper(CallExpr* super_call) {
    if (!current_class)
        errorAt("E0113", "super() can only be used inside a method of a class", super_call);
    if (current_class->base_class.empty())
        errorAt("E0113", "class '" + current_class->name + "' has no base class, so super() has nothing to refer to",
                super_call, "write the base class in the class header: class " + current_class->name + "(Base):");
    if (!super_call->arguments.empty())
        errorAt("E0113", "super() takes no arguments in PyCPP", super_call,
                "write super().method(...); it always refers to the base of the current class");
}

// Built-in method calls (str/list/dict/set) and calls of methods of PyCPP classes.
void SemanticAnalyzer::checkMethodCall(CallExpr* call, MemberAccess* member) {
    std::string object_type = getExpressionType(member->object.get());
    if (!object_type.empty() && object_type.back() == '*') object_type.pop_back();
    const std::string& name = member->member;

    if (const ClassSymbol* cls = classOf(object_type); cls && !cls->is_struct) {
        if (const FunctionSymbol* m = findMethod(object_type, name)) {
            if (!m->generic)
                checkCallArguments(call, "method '" + object_type + "." + name + "'", *m,
                                   static_cast<int>(name.size()));
            return;
        }
        if (findAttribute(object_type, name) || !classFullyKnown(object_type)) return;   // a callable member
        std::vector<std::string> known;
        for (std::string c = object_type; const ClassSymbol* k = classOf(c); c = k->base_class) {
            for (const auto& kv : k->methods) known.push_back(kv.first);
            if (k->base_class.empty()) break;
        }
        const std::string guess = pycpp::closestMatch(name, known);
        error("E0112", "no method named '" + name + "' in class '" + object_type + "'", member->line, member->column,
              guess.empty() ? "define it in the class with `def " + name + "(self, ...):`"
                            : "a method with a similar name exists: '" + guess + "'",
              static_cast<int>(name.size()));
    }

    pycpp::Receiver r;
    if (!receiverOf(object_type, r)) return;
    const pycpp::BuiltinMethod* bm = pycpp::findBuiltinMethod(r, name);
    if (!bm) {
        // C++ members of std::string / std::vector / std::map remain available
        static const std::unordered_set<std::string> cpp_members = {
            "size", "empty", "length", "c_str", "data", "push_back", "pop_back", "emplace_back", "back",
            "front", "begin", "end", "at", "substr", "reserve", "resize", "erase", "contains", "emplace",
            "rbegin", "rend", "capacity", "shrink_to_fit", "swap", "assign", "compare", "find_first_of",
            "find_last_of", "find_first_not_of", "find_last_not_of", "lower_bound", "upper_bound",
            "insert", "count", "find" };
        if (cpp_members.count(name)) return;
        std::vector<std::string> known;
        for (const auto& m : pycpp::builtinMethods()) if (m.receiver == r) known.push_back(m.name);
        const std::string guess = pycpp::closestMatch(name, known);
        error("E0112", std::string("'") + pycpp::receiverName(r) + "' has no method named '" + name + "'",
              member->line, member->column,
              guess.empty() ? std::string("see the README for the supported ") + pycpp::receiverName(r) + " methods"
                            : "did you mean '" + guess + "'?",
              static_cast<int>(name.size()));
    }
    const size_t given = call->arguments.size();
    if (static_cast<int>(given) < bm->min_args || static_cast<int>(given) > bm->max_args) {
        const std::string expected = bm->min_args == bm->max_args
            ? std::to_string(bm->min_args) + (bm->min_args == 1 ? " argument" : " arguments")
            : std::to_string(bm->min_args) + " to " + std::to_string(bm->max_args) + " arguments";
        error("E0106", std::string("method '") + pycpp::receiverName(r) + "." + name + "' takes " + expected + " but " +
              std::to_string(given) + (given == 1 ? " was" : " were") + " supplied",
              member->line, member->column, "", static_cast<int>(name.size()));
    }

    // Argument types that are certain: list element, dict key, str arguments
    std::vector<std::string> expected(given, "");
    const std::string elem = elementTypeOf(object_type);
    if (r == pycpp::Receiver::List) {
        if (name == "append" || name == "remove" || name == "index" || name == "count") expected[0] = elem;
        if (name == "insert") { expected[0] = "int"; expected[1] = elem; }
        if (name == "pop" && given == 1) expected[0] = "int";
    }
    else if (r == pycpp::Receiver::Set) {
        if (given == 1) expected[0] = elem;
    }
    else if (r == pycpp::Receiver::Dict) {
        const auto kv = mapTypes(object_type);
        if (name == "get" || name == "pop" || name == "setdefault") {
            expected[0] = kv.first;
            if (given == 2) expected[1] = kv.second;
        }
    }
    else {   // str
        for (size_t i = 0; i < given; ++i) expected[i] = "str";
        if (name == "join") expected[0] = "";
        if (name == "center" || name == "ljust" || name == "rjust" || name == "zfill") expected[0] = "int";
        if ((name == "split" || name == "rsplit") && given == 2) expected[1] = "int";
        if (name == "replace" && given == 3) expected[2] = "int";
    }
    for (size_t i = 0; i < given; ++i) {
        if (expected[i].empty()) continue;
        Expr* arg = call->arguments[i].get();
        const std::string found = getExpressionType(arg);
        const bool known = (isSimpleType(expected[i]) || classes.count(expected[i])) &&
                           (isSimpleType(found) || classes.count(found));
        if (known && !typesAreCompatible(expected[i], found))
            errorAt("E0107", std::string("mismatched types: argument ") + std::to_string(i + 1) + " of '" +
                    pycpp::receiverName(r) + "." + name + "' expects " + friendlyType(expected[i]) + ", found " +
                    friendlyType(found), arg);
        checkIntegerTarget(arg, expected[i], std::string("passed to '") + pycpp::receiverName(r) + "." + name + "'");
    }
}

// The `/` (or float literal) inside a value that ends up in an integer.
static Expr* fractionalPart(Expr* e, bool& is_division) {
    if (!e) return nullptr;
    if (auto b = dynamic_cast<BinaryOp*>(e)) {
        if (b->op == "/") { is_division = true; return b; }
        if (b->op == "+" || b->op == "-" || b->op == "*" || b->op == "%" || b->op == "**") {
            if (Expr* l = fractionalPart(b->left.get(), is_division)) return l;
            return fractionalPart(b->right.get(), is_division);
        }
        return nullptr;
    }
    if (auto u = dynamic_cast<UnaryOp*>(e)) {
        if (u->op == "-" || u->op == "+") return fractionalPart(u->operand.get(), is_division);
        return nullptr;
    }
    if (auto t = dynamic_cast<TernaryOp*>(e)) {
        if (Expr* a = fractionalPart(t->then_expr.get(), is_division)) return a;
        return fractionalPart(t->else_expr.get(), is_division);
    }
    if (dynamic_cast<FloatLiteral*>(e)) { is_division = false; return e; }
    return nullptr;
}

// Warn when a true division (or a float literal) flows into an integer.
void SemanticAnalyzer::checkIntegerTarget(Expr* value, const std::string& target_type, const std::string& what) {
    if (!isIntegerType(target_type) || !value) return;
    bool is_division = false;
    Expr* culprit = fractionalPart(value, is_division);
    if (!culprit) return;
    if (is_division) {
        warningAt("W0008", "'/' always produces a float, which is truncated when " + what, culprit,
                  "use '//' for integer (floor) division, or int(...) to truncate explicitly");
    }
    else if (culprit == value) {   // a plain float literal
        warningAt("W0004", "this value is a float but it is " + what, culprit,
                  "the fractional part is lost; write an integer literal, or use a float variable");
    }
}

// `x = value` where no x is in scope: in Python that declares x, so it becomes
// `x: <type of value> = value`. Only inside functions, where Python has local variables.
void SemanticAnalyzer::inferDeclaration(std::unique_ptr<Stmt>& stmt) {
    if (!current_function) return;
    auto es = dynamic_cast<ExprStmt*>(stmt.get());
    if (!es) return;
    auto bin = dynamic_cast<BinaryOp*>(es->expr.get());
    if (!bin || bin->op != "=") return;
    auto id = dynamic_cast<Identifier*>(bin->left.get());
    if (!id || id->name == "self" || id->name.find("::") != std::string::npos) return;
    if (findSymbol(id->name) || functions.count(id->name) || classes.count(id->name)) return;
    if (current_class && findAttribute(current_class->name, id->name)) return;   // implicit self member
    if (auto chained = dynamic_cast<BinaryOp*>(bin->right.get()); chained && chained->op == "=") return;

    auto decl = std::make_unique<VarDecl>();
    decl->var_name = id->name;
    decl->line = id->line;
    decl->column = id->column;
    decl->span_line = bin->span_line;
    decl->span_column = bin->span_column;
    decl->span_end_line = bin->span_end_line;
    decl->span_end_column = bin->span_end_column;
    decl->value = std::move(bin->right);
    decl->inferred = true;
    stmt = std::move(decl);
}

void SemanticAnalyzer::analyzeBody(std::vector<std::unique_ptr<Stmt>>& body, bool guarded) {
    for (auto& stmt : body) {
        inferDeclaration(stmt);
        if (guarded) analyzeGuarded(stmt.get());
        else analyzeStatement(stmt.get());
    }
}

// ---- "every path returns" ----

static bool isTerminatingCall(Stmt* s) {
    auto es = dynamic_cast<ExprStmt*>(s);
    if (!es) return false;
    auto call = dynamic_cast<CallExpr*>(es->expr.get());
    if (!call || call->is_subscript) return false;
    auto id = dynamic_cast<Identifier*>(call->callee.get());
    return id && (id->name == "exit" || id->name == "quit" || id->name == "abort" ||
                  id->name == "std::exit" || id->name == "std::abort");
}

// `break` that leaves this loop (not one inside a nested loop)
static bool breaksOut(const std::vector<std::unique_ptr<Stmt>>& body) {
    for (auto& st : body) {
        Stmt* s = st.get();
        if (dynamic_cast<BreakStmt*>(s)) return true;
        if (auto f = dynamic_cast<IfStmt*>(s)) {
            if (breaksOut(f->then_branch) || breaksOut(f->else_branch)) return true;
            for (auto& e : f->elif_branches) if (breaksOut(e.second)) return true;
        }
        else if (auto t = dynamic_cast<TryStmt*>(s)) {
            if (breaksOut(t->body) || breaksOut(t->else_body) || breaksOut(t->finally_body)) return true;
            for (auto& h : t->handlers) if (breaksOut(h.body)) return true;
        }
        else if (auto m = dynamic_cast<MatchStmt*>(s)) {
            for (auto& c : m->cases) if (breaksOut(c.body)) return true;
        }
        else if (auto u = dynamic_cast<UnsafeBlock*>(s)) { if (breaksOut(u->body)) return true; }
        else if (auto b = dynamic_cast<Block*>(s)) { if (breaksOut(b->statements)) return true; }
    }
    return false;
}

bool SemanticAnalyzer::terminates(const std::vector<std::unique_ptr<Stmt>>& body) {
    for (auto& st : body)
        if (terminates(st.get())) return true;   // anything after it is unreachable
    return false;
}

// Control never continues past this statement (it returns, raises, exits or loops forever).
bool SemanticAnalyzer::terminates(Stmt* s) {
    if (dynamic_cast<ReturnStmt*>(s) || dynamic_cast<RaiseStmt*>(s)) return true;
    if (isTerminatingCall(s)) return true;
    if (auto f = dynamic_cast<IfStmt*>(s)) {
        if (f->else_branch.empty() || !terminates(f->then_branch) || !terminates(f->else_branch)) return false;
        for (auto& e : f->elif_branches) if (!terminates(e.second)) return false;
        return true;
    }
    if (auto w = dynamic_cast<WhileStmt*>(s)) {
        auto lit = dynamic_cast<BoolLiteral*>(w->condition.get());
        auto num = dynamic_cast<IntLiteral*>(w->condition.get());
        const bool forever = (lit && lit->value) || (num && num->value != 0);
        return forever && !breaksOut(w->body);
    }
    if (auto t = dynamic_cast<TryStmt*>(s)) {
        if (terminates(t->finally_body)) return true;
        const bool body_ok = terminates(t->body) || (!t->else_body.empty() && terminates(t->else_body));
        if (!body_ok) return false;
        for (auto& h : t->handlers) if (!terminates(h.body)) return false;
        return true;
    }
    if (auto m = dynamic_cast<MatchStmt*>(s)) {
        bool has_default = false;
        for (auto& c : m->cases) {
            if (c.is_default) has_default = true;
            if (!terminates(c.body)) return false;
        }
        return has_default;
    }
    if (auto u = dynamic_cast<UnsafeBlock*>(s)) return terminates(u->body);
    if (auto b = dynamic_cast<Block*>(s)) return terminates(b->statements);
    return false;
}
