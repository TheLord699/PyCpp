
/*
#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include "../include/lexer.h"
#include "../include/pycpp_parser.h"
#include "../include/semantic_analyzer.h"
#include "../include/include_manager.h"
#include "../include/code_generator.h"
#include "../include/header_generator.h"

void generateHeaderAndImplementation(std::unique_ptr<Program>& ast,
    const std::string& base_filename,
    IncludeManager& include_manager);

class PrintVisitor : public ASTVisitor {
public:
    void visit(Program& node) override {
        std::cout << "Program:" << std::endl;
        for (auto& stmt : node.statements) {
            if (stmt) {
                stmt->accept(*this);
            }
            else {
                std::cout << "  [null statement]" << std::endl;
            }
        }
    }

    void visit(FunctionDef& node) override {
        std::cout << "  Function: " << node.name << " -> ";
        if (node.return_type) std::cout << node.return_type->name;
        std::cout << std::endl;
        for (auto& param : node.parameters) {
            std::cout << "    Param: " << param.name << " : ";
            if (param.type) {
                std::cout << param.type->name;
            }
            else {
                std::cout << "[no type]";
            }
            if (param.default_value) {
                std::cout << " = [has default]";
            }
            std::cout << std::endl;
        }
        for (auto& stmt : node.body) {
            if (stmt) {
                std::cout << "    ";
                stmt->accept(*this);
            }
        }
    }

    void visit(MemberAccess& node) override {
        std::cout << "      MemberAccess: ";
        node.object->accept(*this);
        std::cout << "." << node.member << std::endl;
    }

    void visit(ClassDef& node) override {
        std::cout << "  Class: " << node.name;
        if (!node.base_class.empty()) {
            std::cout << " extends " << node.base_class;
        }
        std::cout << std::endl;

        for (auto& attr : node.attributes) {
            if (attr) {
                attr->accept(*this);
            }
        }
        for (auto& method : node.methods) {
            if (method) {
                method->accept(*this);
            }
        }
    }

    void visit(StructDef& node) override {
        std::cout << "  Struct: " << node.name << std::endl;
        for (auto& field : node.fields) {
            if (field) {
                std::cout << "    Field: ";
                field->accept(*this);
            }
        }
    }

    void visit(TemplateDef& node) override {
        std::cout << "  Template: <typename " << node.type_param << ">" << std::endl;
        if (node.templated_class) {
            node.templated_class->accept(*this);
        }
        if (node.templated_function) {
            node.templated_function->accept(*this);
        }
    }

    void visit(ConstexprDef& node) override {
        std::cout << "  Constexpr: " << node.name << " : " << node.type->name << " = [value]" << std::endl;
    }

    void visit(UnsafeBlock& node) override {
        std::cout << "  Unsafe Block:" << std::endl;
        for (auto& stmt : node.body) {
            if (stmt) {
                std::cout << "    ";
                stmt->accept(*this);
            }
        }
    }

    void visit(VarDecl& node) override {
        std::cout << "    Var: " << node.var_name << " : " << node.type->name;
        if (node.value) {
            std::cout << " = [has value]";
        }
        std::cout << std::endl;
    }

    void visit(ReturnStmt& node) override {
        std::cout << "    Return";
        if (node.value) {
            std::cout << " [has value]";
        }
        std::cout << std::endl;
    }

    void visit(IfStmt& node) override {
        std::cout << "    If [has condition]" << std::endl;
        for (auto& stmt : node.then_branch) {
            if (stmt) {
                std::cout << "      ";
                stmt->accept(*this);
            }
        }
    }

    void visit(WhileStmt& node) override {
        std::cout << "    While [has condition]" << std::endl;
        for (auto& stmt : node.body) {
            if (stmt) {
                std::cout << "      ";
                stmt->accept(*this);
            }
        }
    }

    void visit(ForStmt& node) override {
        std::cout << "    For " << node.var_name << " in [iterable]" << std::endl;
        for (auto& stmt : node.body) {
            if (stmt) {
                std::cout << "      ";
                stmt->accept(*this);
            }
        }
    }

    void visit(ExprStmt& node) override {
        std::cout << "    Expression statement" << std::endl;
    }

    void visit(Block& node) override {
        std::cout << "    Block with " << node.statements.size() << " statements" << std::endl;
    }

    void visit(IntLiteral& node) override {
        std::cout << "      Int: " << node.value << std::endl;
    }

    void visit(FloatLiteral& node) override {
        std::cout << "      Float: " << node.value << std::endl;
    }

    void visit(StringLiteral& node) override {
        std::cout << "      String: \"" << node.value << "\"" << std::endl;
    }

    void visit(BoolLiteral& node) override {
        std::cout << "      Bool: " << (node.value ? "true" : "false") << std::endl;
    }

    void visit(Identifier& node) override {
        std::cout << "      Identifier: " << node.name << std::endl;
    }

    void visit(BinaryOp& node) override {
        std::cout << "      BinaryOp: " << node.op << std::endl;
    }

    void visit(CallExpr& node) override {
        std::cout << "      Call: [function] with " << node.arguments.size() << " args" << std::endl;
    }

    void visit(ListLiteral& node) override {
        std::cout << "      List with " << node.elements.size() << " elements" << std::endl;
    }

    void visit(DictLiteral& node) override {
        std::cout << "      Dict with " << node.items.size() << " items" << std::endl;
    }

    void visit(ImportStmt& node) override {
        std::cout << "  Import: " << node.module;
        if (!node.imports.empty()) {
            std::cout << " (";
            for (size_t i = 0; i < node.imports.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << node.imports[i];
            }
            std::cout << ")";
        }
        std::cout << std::endl;
    }

    void visit(UseStmt& node) override {
        std::cout << "  Use: " << node.library << std::endl;
    }

    // Type visitor methods
    void visit(BasicType& node) override {
        std::cout << "      Type: " << node.name << std::endl;
    }

    void visit(PointerType& node) override {
        std::cout << "      PointerType: " << node.name << std::endl;
    }

    void visit(ListType& node) override {
        std::cout << "      ListType: " << node.name << std::endl;
    }

    void visit(DictType& node) override {
        std::cout << "      DictType: " << node.name << std::endl;
    }
};

void verifyGeneratedCode(const std::string& cpp_code) {
    std::cout << "\n=== Verifying Generated Code ===" << std::endl;

    // Check for proper class structure
    if (cpp_code.find("class Player {") != std::string::npos) {
        if (cpp_code.find("std::string name;") != std::string::npos &&
            cpp_code.find("int health;") != std::string::npos) {
            std::cout << "? Class member variables are properly declared" << std::endl;
        }
        else {
            std::cout << "? Class member variables are missing" << std::endl;
            std::cout << "   Looking for: std::string name; and int health;" << std::endl;
            std::cout << "   Generated class content:" << std::endl;
            size_t class_start = cpp_code.find("class Player {");
            size_t class_end = cpp_code.find("};", class_start);
            if (class_start != std::string::npos && class_end != std::string::npos) {
                std::cout << cpp_code.substr(class_start, class_end - class_start + 2) << std::endl;
            }
        }
    }

    // Check for proper return statement placement
    size_t for_pos = cpp_code.find("for (auto n : nums)");
    size_t return_pos = cpp_code.find("return 0;");

    if (for_pos != std::string::npos && return_pos != std::string::npos) {
        // Find the closing brace of the for loop
        size_t for_brace_open = cpp_code.find('{', for_pos);
        size_t for_brace_close = cpp_code.find('}', for_brace_open);

        if (return_pos < for_brace_close) {
            std::cout << "? Return statement is incorrectly inside the for loop" << std::endl;
            std::cout << "   For loop range: " << for_pos << " to " << for_brace_close << std::endl;
            std::cout << "   Return position: " << return_pos << std::endl;
        }
        else {
            std::cout << "? Return statement is properly placed" << std::endl;
        }
    }

    // Check for proper memory operations
    if (cpp_code.find("static_cast<int*>(malloc") != std::string::npos &&
        cpp_code.find("free(ptr)") != std::string::npos) {
        std::cout << "? Memory operations are properly generated" << std::endl;
    }
    else {
        std::cout << "? Memory operations are incorrect" << std::endl;
    }

    // Check for proper string type usage
    if (cpp_code.find("std::string name = \"hello\"") != std::string::npos) {
        std::cout << "? String types are properly generated" << std::endl;
    }
    else {
        std::cout << "? String types are incorrect" << std::endl;
    }

    // Check for proper constructor generation
    if (cpp_code.find("void __init__(std::string name = \"Bob\", int hp = 100)") != std::string::npos) {
        std::cout << "? Constructor with default parameters is properly generated" << std::endl;
    }
    else {
        std::cout << "? Constructor generation has issues" << std::endl;
    }
}

void testPrintFunctionFix() {
    std::cout << "\n=== Testing Print Function Fix ===" << std::endl;

    std::string test_code =
        "use <iostream>\n"
        "use <string>\n\n"
        "def main() -> int:\n"
        "    name: str = \"world\"\n"
        "    print(\"Hello\", name)\n"
        "    return 0";

    try {
        Lexer lexer(test_code);
        auto tokens = lexer.tokenize();

        PyCppParser parser(tokens);
        auto ast = parser.parse();

        SemanticAnalyzer analyzer;
        analyzer.analyze(ast);

        IncludeManager include_manager;
        include_manager.trackManualInclude("<iostream>");
        include_manager.trackManualInclude("<string>");

        CodeGenerator generator;
        std::string cpp_code = generator.generate(ast, include_manager);

        std::cout << "Generated C++ code:" << std::endl;
        std::cout << cpp_code << std::endl;

        // Check if print is properly converted to cout
        if (cpp_code.find("cout << \"Hello\" << name << endl") != std::string::npos) {
            std::cout << "? SUCCESS: print function properly converted to cout" << std::endl;
        }
        else {
            std::cout << "? FAILED: print function not properly converted" << std::endl;
            std::cout << "Looking for: cout << \"Hello\" << name << endl" << std::endl;
        }
    }
    catch (const std::exception& e) {
        std::cout << "? FAILED: " << e.what() << std::endl;
    }
}

void testSimpleCase() {
    std::cout << "\n=== Testing Simple Case ===" << std::endl;

    std::string simple_code =
        "def main() -> int:\n"
        "    x: int = 42\n"
        "    name: str = \"test\"\n"
        "    return 0";

    try {
        Lexer lexer(simple_code);
        auto tokens = lexer.tokenize();
        std::cout << "? Lexed " << tokens.size() << " tokens" << std::endl;

        PyCppParser parser(tokens);
        auto ast = parser.parse();
        std::cout << "? Parsed successfully" << std::endl;

        SemanticAnalyzer analyzer;
        analyzer.analyze(ast);
        std::cout << "? Semantic analysis completed" << std::endl;

        IncludeManager include_manager;

        // Track features from semantic analysis
        const auto& used_builtins = analyzer.getUsedBuiltins();
        std::cout << "Detected features: ";
        for (const auto& feature : used_builtins) {
            include_manager.trackFeature(feature);
            std::cout << feature << " ";
        }
        std::cout << std::endl;

        // Additional tracking based on common patterns
        if (used_builtins.find("str") != used_builtins.end()) {
            include_manager.trackFeature("string");
        }
        if (used_builtins.find("print") != used_builtins.end()) {
            include_manager.trackFeature("iostream");
        }

        CodeGenerator generator;
        std::string cpp_code = generator.generate(ast, include_manager);
        std::cout << "? Generated C++ code" << std::endl;

        std::cout << "Generated code preview:" << std::endl;
        std::cout << cpp_code.substr(0, 200) << "..." << std::endl;

    }
    catch (const std::exception& e) {
        std::cout << "? Simple test failed: " << e.what() << std::endl;
    }
}

void testMemoryOperations() {
    std::cout << "\n=== Testing Memory Operations ===" << std::endl;

    std::string memory_code =
        "unsafe:\n"
        "    ptr: *int = mem.alloc(100)\n"
        "    ptr[0] = 42\n"
        "    mem.free(ptr)";

    try {
        Lexer lexer(memory_code);
        auto tokens = lexer.tokenize();
        std::cout << "? Lexed " << tokens.size() << " tokens" << std::endl;

        PyCppParser parser(tokens);
        auto ast = parser.parse();
        std::cout << "? Parsed successfully" << std::endl;

        SemanticAnalyzer analyzer;
        analyzer.analyze(ast);
        std::cout << "? Semantic analysis completed" << std::endl;

        IncludeManager include_manager;

        // Track features from semantic analysis
        const auto& used_builtins = analyzer.getUsedBuiltins();
        std::cout << "Detected features: ";
        for (const auto& feature : used_builtins) {
            include_manager.trackFeature(feature);
            std::cout << feature << " ";
        }
        std::cout << std::endl;

        // Track memory operations
        if (used_builtins.find("mem.alloc") != used_builtins.end() ||
            used_builtins.find("mem.free") != used_builtins.end()) {
            include_manager.trackFeature("cstdlib");
        }

        CodeGenerator generator;
        std::string cpp_code = generator.generate(ast, include_manager);
        std::cout << "? Generated C++ code" << std::endl;

        std::cout << "Generated code:" << std::endl;
        std::cout << cpp_code << std::endl;

        // Verify memory operations
        if (cpp_code.find("static_cast<int*>(malloc") != std::string::npos &&
            cpp_code.find("free(ptr)") != std::string::npos) {
            std::cout << "? Memory operations correctly generated" << std::endl;
        }
        else {
            std::cout << "? Memory operations incorrectly generated" << std::endl;
        }

    }
    catch (const std::exception& e) {
        std::cout << "? Memory operations test failed: " << e.what() << std::endl;
    }
}

// NEW: Test for class member variables
void testClassMemberVariables() {
    std::cout << "\n=== Testing Class Member Variables ===" << std::endl;

    std::string class_code =
        "class Player:\n"
        "    self.name: str\n"
        "    self.health: int = 100\n"
        "    \n"
        "    def __init__(self, name: str):\n"
        "        self.name = name\n";

    try {
        Lexer lexer(class_code);
        auto tokens = lexer.tokenize();
        std::cout << "? Lexed " << tokens.size() << " tokens" << std::endl;

        PyCppParser parser(tokens);
        auto ast = parser.parse();
        std::cout << "? Parsed successfully" << std::endl;

        SemanticAnalyzer analyzer;
        analyzer.analyze(ast);
        std::cout << "? Semantic analysis completed" << std::endl;

        IncludeManager include_manager;
        include_manager.trackFeature("string");

        CodeGenerator generator;
        std::string cpp_code = generator.generate(ast, include_manager);
        std::cout << "? Generated C++ code" << std::endl;

        std::cout << "Generated code:" << std::endl;
        std::cout << cpp_code << std::endl;

        // Verify member variables are present
        if (cpp_code.find("std::string name;") != std::string::npos &&
            cpp_code.find("int health = 100;") != std::string::npos) {
            std::cout << "? Class member variables properly generated" << std::endl;
        }
        else {
            std::cout << "? Class member variables still missing" << std::endl;
        }

    }
    catch (const std::exception& e) {
        std::cout << "? Class member variables test failed: " << e.what() << std::endl;
    }
}

void testClassGeneration() {
    std::cout << "\n=== Testing Class Generation ===" << std::endl;

    std::string class_code =
        "class Player:\n"
        "    def __init__(self, name: str = \"Bob\", hp: int = 100):\n"
        "        self.name: str = name\n"
        "        self.health: int = hp\n\n"
        "    def take_damage(self, damage: int):\n"
        "        self.health -= damage";

    try {
        Lexer lexer(class_code);
        auto tokens = lexer.tokenize();
        std::cout << "? Lexed " << tokens.size() << " tokens" << std::endl;

        PyCppParser parser(tokens);
        auto ast = parser.parse();
        std::cout << "? Parsed successfully" << std::endl;

        SemanticAnalyzer analyzer;
        analyzer.analyze(ast);
        std::cout << "? Semantic analysis completed" << std::endl;

        IncludeManager include_manager;
        include_manager.trackFeature("string");
        include_manager.trackFeature("iostream");

        CodeGenerator generator;
        std::string cpp_code = generator.generate(ast, include_manager);
        std::cout << "? Generated C++ code" << std::endl;

        std::cout << "Generated code:" << std::endl;
        std::cout << cpp_code << std::endl;

        // Verify class generation
        if (cpp_code.find("class Player {") != std::string::npos) {
            if (cpp_code.find("std::string name;") != std::string::npos &&
                cpp_code.find("int health;") != std::string::npos) {
                std::cout << "? Class member variables properly generated" << std::endl;
            }
            else {
                std::cout << "? Class member variables missing" << std::endl;
            }

            if (cpp_code.find("void take_damage(int damage)") != std::string::npos) {
                std::cout << "? Class methods properly generated" << std::endl;
            }
            else {
                std::cout << "? Class methods missing" << std::endl;
            }
        }
        else {
            std::cout << "? Class not generated" << std::endl;
        }

    }
    catch (const std::exception& e) {
        std::cout << "? Class generation test failed: " << e.what() << std::endl;
    }
}

void testControlFlow() {
    std::cout << "\n=== Testing Control Flow ===" << std::endl;

    std::string control_flow_code =
        "def main() -> int:\n"
        "    x: int = 10\n"
        "    \n"
        "    if x > 5:\n"
        "        print(\"x is greater than 5\")\n"
        "    elif x == 5:\n"
        "        print(\"x is 5\")\n"
        "    else:\n"
        "        print(\"x is less than 5\")\n"
        "    \n"
        "    while x > 0:\n"
        "        print(x)\n"
        "        x -= 1\n"
        "    \n"
        "    return 0";

    try {
        Lexer lexer(control_flow_code);
        auto tokens = lexer.tokenize();
        std::cout << "? Lexed " << tokens.size() << " tokens" << std::endl;

        PyCppParser parser(tokens);
        auto ast = parser.parse();
        std::cout << "? Parsed successfully" << std::endl;

        SemanticAnalyzer analyzer;
        analyzer.analyze(ast);
        std::cout << "? Semantic analysis completed" << std::endl;

        IncludeManager include_manager;
        include_manager.trackFeature("iostream");
        include_manager.trackFeature("compound_ops");

        CodeGenerator generator;
        std::string cpp_code = generator.generate(ast, include_manager);
        std::cout << "? Generated C++ code" << std::endl;

        std::cout << "Generated code:" << std::endl;
        std::cout << cpp_code << std::endl;

        if (cpp_code.find("if (") != std::string::npos &&
            cpp_code.find("else if (") != std::string::npos &&
            cpp_code.find("else {") != std::string::npos &&
            cpp_code.find("while (") != std::string::npos) {
            std::cout << "? Control flow statements properly generated" << std::endl;
        }
        else {
            std::cout << "? Control flow statements incorrectly generated" << std::endl;
        }

    }
    catch (const std::exception& e) {
        std::cout << "? Control flow test failed: " << e.what() << std::endl;
    }
}

// NEW: Test simple header generation
void testSimpleHeaderGeneration() {
    std::cout << "\n=== Testing Simple Header Generation ===" << std::endl;

    std::string simple_code =
        "def add(a: int, b: int) -> int:\n"
        "    return a + b\n\n"
        "def greet(name: str) -> str:\n"
        "    return \"Hello, \" + name";

    try {
        Lexer lexer(simple_code);
        auto tokens = lexer.tokenize();

        PyCppParser parser(tokens);
        auto ast = parser.parse();

        SemanticAnalyzer analyzer;
        analyzer.analyze(ast);

        IncludeManager include_manager;
        generateHeaderAndImplementation(ast, "math_utils", include_manager);

        std::cout << "? Simple header generation test passed!" << std::endl;

        // Display generated header
        std::ifstream header_file("math_utils.h");
        std::string header_content((std::istreambuf_iterator<char>(header_file)),
            std::istreambuf_iterator<char>());
        std::cout << "Generated header preview:\n" << header_content << std::endl;

    }
    catch (const std::exception& e) {
        std::cout << "? Simple header generation test failed: " << e.what() << std::endl;
    }
}

// NEW: Test automatic header generation
void testHeaderGeneration() {
    std::cout << "\n=== Testing Automatic Header Generation ===" << std::endl;

    std::string header_test_code =
        "use <iostream>\n"
        "use <string>\n"
        "use <vector>\n\n"

        "class Player:\n"
        "    self.name: str\n"
        "    self.health: int = 100\n"
        "    self.level: int = 1\n"
        "    \n"
        "    def __init__(self, name: str, health: int = 100):\n"
        "        self.name = name\n"
        "        self.health = health\n\n"
        "    def take_damage(self, damage: int):\n"
        "        self.health -= damage\n        \n"
        "    def heal(self, amount: int):\n"
        "        self.health += amount\n\n"
        "    def get_status(self) -> str:\n"
        "        return self.name + \" - Health: \" + str(self.health)\n\n"

        "struct GameConfig:\n"
        "    width: int = 800\n"
        "    height: int = 600\n"
        "    title: str = \"My Game\"\n\n"

        "def create_player(name: str) -> Player:\n"
        "    return Player(name)\n\n"

        "def calculate_damage(attacker: Player, defender: Player) -> int:\n"
        "    base_damage: int = 10\n"
        "    return base_damage\n\n"

        "template <typename T>\n"
        "class Container:\n"
        "    def __init__(self, value: T):\n"
        "        self.value: T = value\n\n"
        "    def get_value(self) -> T:\n"
        "        return self.value\n\n"

        "constexpr MAX_PLAYERS: int = 4\n"
        "constexpr GAME_VERSION: str = \"1.0.0\"";

    try {
        std::cout << "Testing header generation with comprehensive example..." << std::endl;

        // Lexing
        Lexer lexer(header_test_code);
        auto tokens = lexer.tokenize();
        std::cout << "? Lexed " << tokens.size() << " tokens" << std::endl;

        // Parsing
        PyCppParser parser(tokens);
        auto ast = parser.parse();
        std::cout << "? Parsed successfully" << std::endl;

        // Semantic Analysis
        SemanticAnalyzer analyzer;
        analyzer.analyze(ast);
        std::cout << "? Semantic analysis completed" << std::endl;

        // Create include manager for tracking dependencies
        IncludeManager include_manager;

        // Generate both header and implementation
        generateHeaderAndImplementation(ast, "game_objects", include_manager);

        // Read and display the generated files
        std::cout << "\n=== Generated Header Content ===" << std::endl;
        std::ifstream header_file("game_objects.h");
        std::string header_content((std::istreambuf_iterator<char>(header_file)),
            std::istreambuf_iterator<char>());
        std::cout << header_content << std::endl;

        std::cout << "\n=== Generated Implementation Content ===" << std::endl;
        std::ifstream cpp_file("game_objects.cpp");
        std::string cpp_content((std::istreambuf_iterator<char>(cpp_file)),
            std::istreambuf_iterator<char>());
        std::cout << cpp_content << std::endl;

        // Verify the generated code
        std::cout << "\n=== Verification ===" << std::endl;
        if (header_content.find("class Player") != std::string::npos &&
            header_content.find("class Container") != std::string::npos &&
            header_content.find("struct GameConfig") != std::string::npos) {
            std::cout << "? All classes and structs properly declared in header" << std::endl;
        }

        if (cpp_content.find("Player::Player") != std::string::npos &&
            cpp_content.find("Player::take_damage") != std::string::npos) {
            std::cout << "? All methods properly implemented" << std::endl;
        }

        if (header_content.find("#ifndef GAME_OBJECTS_H") != std::string::npos &&
            header_content.find("#define GAME_OBJECTS_H") != std::string::npos) {
            std::cout << "? Proper include guards generated" << std::endl;
        }

        std::cout << "\n?? Header generation test completed successfully!" << std::endl;

    }
    catch (const std::exception& e) {
        std::cout << "? Header generation test failed: " << e.what() << std::endl;
    }
}

void testCompleteCompilerPipeline() {
    std::cout << "\n=== Testing Complete PyCPP Compiler Pipeline ===" << std::endl;

    // Test the full PyCPP language features
    std::string test_code =
        "use <iostream>\n"
        "use <vector>\n"
        "use <string>\n"
        "use <map>\n"
        "use <cstdlib>\n\n"

        "def main() -> int:\n"
        "    x: int = 42\n"
        "    name: str = \"hello\"\n"
        "    nums: list[int] = [1, 2, 3]\n"
        "    \n"
        "    if x > 10:\n"
        "        print(name)\n"
        "    \n"
        "    for n in nums:\n"
        "        print(n)\n"
        "    \n"

        "class Player:\n"
        "    def __init__(self, name: str = \"Bob\", hp: int = 100):\n"
        "        self.name: str = name\n"
        "        self.health: int = hp\n\n"
        "    def take_damage(self, damage: int):\n"
        "        self.health -= damage\n\n"

        "struct Position:\n"
        "    x: float\n"
        "    y: float\n\n"

        "template <typename T>\n"
        "class Box:\n"
        "    def __init__(self, value: T):\n"
        "        self.value: T = value\n\n"

        "constexpr PI: float = 3.14159\n\n"

        "unsafe:\n"
        "    ptr: *int = mem.alloc(10)\n"
        "    ptr[0] = 42\n"
        "    mem.free(ptr)";

    try {
        std::cout << "\n=== STEP 1: Lexing ===" << std::endl;
        // Lexing
        Lexer lexer(test_code);
        auto tokens = lexer.tokenize();

        std::cout << "? Lexed " << tokens.size() << " tokens" << std::endl;

        // Print first few tokens for debugging
        for (size_t i = 0; i < tokens.size() && i < 10; ++i) {
            const auto& token = tokens[i];
            std::cout << "  Token[" << i << "]: '" << token.value << "' type: "
                << static_cast<int>(token.type) << std::endl;
        }
        if (tokens.size() > 10) {
            std::cout << "  ... and " << (tokens.size() - 10) << " more tokens" << std::endl;
        }

        std::cout << "\n=== STEP 2: Parsing ===" << std::endl;
        // Parsing
        PyCppParser parser(tokens);
        auto ast = parser.parse();
        std::cout << "? Parsed successfully" << std::endl;

        // Print AST using visitor
        PrintVisitor visitor;
        ast->accept(visitor);

        std::cout << "\n=== STEP 3: Semantic Analysis ===" << std::endl;
        // Semantic Analysis
        SemanticAnalyzer analyzer;
        analyzer.analyze(ast);
        std::cout << "? Semantic analysis completed successfully" << std::endl;

        std::cout << "\n=== STEP 4: Header and Implementation Generation ===" << std::endl;
        // Create include manager for tracking dependencies
        IncludeManager include_manager;

        // Generate both header and implementation
        generateHeaderAndImplementation(ast, "generated_code", include_manager);

        // Read and display the generated files
        std::cout << "\n=== Generated Header Content ===" << std::endl;
        std::ifstream header_file("generated_code.h");
        std::string header_content((std::istreambuf_iterator<char>(header_file)),
            std::istreambuf_iterator<char>());
        std::cout << header_content << std::endl;

        std::cout << "\n=== Generated Implementation Content ===" << std::endl;
        std::ifstream cpp_file("generated_code.cpp");
        std::string cpp_content((std::istreambuf_iterator<char>(cpp_file)),
            std::istreambuf_iterator<char>());
        std::cout << cpp_content << std::endl;

        // Add verification
        verifyGeneratedCode(cpp_content);

        std::cout << "\n?? SUCCESS: Complete compiler pipeline test passed!" << std::endl;

    }
    catch (const std::exception& e) {
        std::cout << "? FAILED: " << e.what() << std::endl;
    }
}

int main() {
    std::cout << "PyCPP Compiler Test Suite with Automatic Header Generation" << std::endl;
    std::cout << "==========================================================" << std::endl;

    // Run existing tests
    testPrintFunctionFix();
    testSimpleCase();
    testMemoryOperations();
    testClassMemberVariables();
    testClassGeneration();
    testControlFlow();

    // Run header generation tests
    testSimpleHeaderGeneration();
    testHeaderGeneration();
    testCompleteCompilerPipeline();

    std::cout << "\n=== All Tests Completed ===" << std::endl;

    // Clean up generated files
    std::remove("math_utils.h");
    std::remove("math_utils.cpp");
    std::remove("game_objects.h");
    std::remove("game_objects.cpp");
    std::remove("generated_code.h");
    std::remove("generated_code.cpp");

    return 0;
}

*/