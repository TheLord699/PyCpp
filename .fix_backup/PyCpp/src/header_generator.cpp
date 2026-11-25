#include "../include/header_generator.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_set>

HeaderGenerator::HeaderGenerator() : indent_level(0) {}

// Helper: extract base filename without path or extension
std::string HeaderGenerator::getBaseFilename(const std::string& filename) {
    std::string base = filename;
    size_t last_slash = base.find_last_of("/\\");
    if (last_slash != std::string::npos)
        base = base.substr(last_slash + 1);
    size_t last_dot = base.find_last_of('.');
    if (last_dot != std::string::npos)
        base = base.substr(0, last_dot);
    return base;
}

// Helper: generate include guard
void HeaderGenerator::generateIncludeGuard(const std::string& filename) {
    std::string guard_name = getBaseFilename(filename);
    if (guard_name.empty()) guard_name = "HEADER"; // Fallback if empty

    // Convert to uppercase and replace non-alphanumeric chars with underscores
    for (char& c : guard_name) {
        if (std::isalnum(c)) c = std::toupper(c);
        else c = '_';
    }
    guard_name += "_H";

    output << "#ifndef " << guard_name << "\n";
    output << "#define " << guard_name << "\n\n";
}

// Main generate function
std::string HeaderGenerator::generate(std::unique_ptr<Program>& program,
    const std::string& filename,
    const std::vector<std::string>& manual_includes) {
    output.str(""); // Clear previous output

    // Generate include guard
    generateIncludeGuard(filename);

    // Minimal comment
    output << "// Minimal header - most code is in .cpp file\n\n";

    // Collect normal classes/structs only (skip templates)
    std::unordered_set<std::string> declared_classes;
    for (auto& stmt : program->statements) {
        if (auto cls = dynamic_cast<ClassDef*>(stmt.get())) {
            declared_classes.insert(cls->name);
        }
        else if (auto struc = dynamic_cast<StructDef*>(stmt.get())) {
            declared_classes.insert(struc->name);
        }
        // Skip template classes entirely
    }

    // Output forward declarations
    if (!declared_classes.empty()) {
        output << "// Forward declarations\n";
        for (const auto& cls_name : declared_classes) {
            output << "class " << cls_name << ";\n";
        }
        output << "\n";
    }

    /*
    // Optional: constexpr declarations if needed
    for (auto& stmt : program->statements) {
        if (auto constexpr_def = dynamic_cast<ConstexprDef*>(stmt.get())) {
            output << "constexpr ";
            generateType(constexpr_def->type.get());
            output << " " << constexpr_def->name << " = ";
            if (constexpr_def->value) generateDefaultValue(constexpr_def->value.get());
            output << ";\n";
        }
    }
    */

    // Close include guard
    std::string guard_name = getBaseFilename(filename);
    if (guard_name.empty()) guard_name = "HEADER";

    for (char& c : guard_name) {
        if (std::isalnum(c)) c = std::toupper(c);
        else c = '_';
    }
    guard_name += "_H";

    output << "#endif // " << guard_name << "\n";

    return output.str();
}

// --- Optional helpers ---

void HeaderGenerator::generateType(Type* type) {
    if (!type) {
        output << "void";
        return;
    }
    if (auto basic_type = dynamic_cast<BasicType*>(type)) {
        if (basic_type->name == "str") output << "std::string";
        else output << basic_type->name;
    }
    else if (auto list_type = dynamic_cast<ListType*>(type)) {
        output << "std::vector<";
        generateType(list_type->element_type.get());
        output << ">";
    }
    else {
        output << "auto";
    }
}

void HeaderGenerator::generateDefaultValue(Expr* expr) {
    if (auto int_lit = dynamic_cast<IntLiteral*>(expr)) output << int_lit->value;
    else if (auto str_lit = dynamic_cast<StringLiteral*>(expr)) output << "\"" << str_lit->value << "\"";
    else if (auto float_lit = dynamic_cast<FloatLiteral*>(expr)) output << float_lit->value;
    else if (auto bool_lit = dynamic_cast<BoolLiteral*>(expr)) output << (bool_lit->value ? "true" : "false");
}
