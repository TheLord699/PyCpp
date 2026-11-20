#include "import_resolver.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <system_error>
#include <sstream>
#include <iterator>
#include "diagnostics.h"

namespace {
std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

// Remove comments without treating # inside quoted strings as a comment.
std::string withoutComment(const std::string& line) {
    char quote = '\0';
    bool escaped = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quote) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == quote) quote = '\0';
        } else if (c == '\'' || c == '"') quote = c;
        else if (c == '#') return line.substr(0, i);
    }
    return line;
}

std::string modulePath(std::string name) {
    name = trim(name);
    // An alias belongs to the imported module, not its filesystem name.
    const auto alias = name.find(" as ");
    if (alias != std::string::npos) name = trim(name.substr(0, alias));
    std::replace(name.begin(), name.end(), '.', '/');
    return name;
}
}

// [diag-fix] a missing module is an error shown on the import line that asked for it
static std::vector<pycpp::Diagnostic>& missingModules() {
    static std::vector<pycpp::Diagnostic> list;
    return list;
}

static void reportMissingModule(const std::string& file, const std::string& module) {
    pycpp::Diagnostic d;
    d.message = "cannot find module '" + module + "'";
    d.file = file;
    std::ifstream in(file);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::istringstream stream(text);
    for (std::string l; std::getline(stream, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        d.snippet_lines.push_back(l);
    }
    std::string dotted = module;
    std::replace(dotted.begin(), dotted.end(), '/', '.');
    for (size_t i = 0; i < d.snippet_lines.size(); ++i) {
        const std::string stmt = trim(withoutComment(d.snippet_lines[i]));
        if (stmt.rfind("import ", 0) != 0 && stmt.rfind("from ", 0) != 0) continue;
        const size_t p = d.snippet_lines[i].find(dotted);
        if (p == std::string::npos) continue;
        d.line = static_cast<int>(i) + 1;
        d.column = static_cast<int>(p) + 1;
        d.length = static_cast<int>(dotted.size());
        break;
    }
    d.label = "no such module";
    d.hint = "looked for '" + module + ".pycpp' next to the importing file and in the import paths; add a folder with -I <path>";
    missingModules().push_back(std::move(d));
}

ImportResolver::ImportResolver() {
    search_paths.emplace_back(".");
    search_paths.emplace_back("./lib");
    search_paths.emplace_back("./src");
}

void ImportResolver::addSearchPath(const std::string& path) {
    if (path.empty()) return;
    std::error_code ec;
    const auto normalized = std::filesystem::weakly_canonical(path, ec);
    const std::string value = (ec ? std::filesystem::path(path).lexically_normal() : normalized).string();
    if (std::find(search_paths.begin(), search_paths.end(), value) == search_paths.end())
        search_paths.push_back(value);
}

std::vector<std::string> ImportResolver::resolveAllDependencies(const std::string& entry_file) {
    std::vector<std::string> all_files;
    processed_files.clear();
    missingModules().clear();
    resolveDependenciesRecursive(entry_file, all_files);
    if (!missingModules().empty()) {
        auto problems = std::move(missingModules());
        missingModules().clear();
        throw pycpp::CompileError(std::move(problems));
    }
    return all_files;
}

void ImportResolver::resolveDependenciesRecursive(const std::string& filename,
                                                   std::vector<std::string>& result) {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(filename, ec);
    const std::string key = (ec ? std::filesystem::path(filename).lexically_normal() : canonical).string();
    if (!processed_files.insert(key).second) return;

    result.push_back(key);
    for (const auto& module_name : extractImports(key)) {
        const std::string module_file = findModuleFile(module_name, key);
        if (module_file.empty()) {
            reportMissingModule(key, module_name);
            continue;
        }
        resolveDependenciesRecursive(module_file, result);
    }
}

std::vector<std::string> ImportResolver::extractImports(const std::string& filename) {
    std::vector<std::string> imports;
    std::ifstream file(filename);
    if (!file) {
        std::cerr << "warning: cannot read source file while resolving imports: " << filename << '\n';
        return imports;
    }

    std::string line;
    while (std::getline(file, line)) {
        const std::string statement = trim(withoutComment(line));
        if (statement.rfind("from ", 0) == 0) {
            const auto import_pos = statement.find(" import ", 5);
            if (import_pos != std::string::npos) {
                const std::string module = modulePath(statement.substr(5, import_pos - 5));
                if (!module.empty()) imports.push_back(module);
            }
        } else if (statement.rfind("import ", 0) == 0) {
            std::string list = statement.substr(7);
            std::size_t begin = 0;
            while (begin < list.size()) {
                const auto comma = list.find(',', begin);
                const std::string module = modulePath(list.substr(begin,
                    comma == std::string::npos ? std::string::npos : comma - begin));
                if (!module.empty()) imports.push_back(module);
                if (comma == std::string::npos) break;
                begin = comma + 1;
            }
        }
    }
    return imports;
}

std::string ImportResolver::findModuleFile(const std::string& module_name,
                                           const std::string& importing_file) {
    const std::string relative_module = modulePath(module_name);
    if (relative_module.empty()) return {};

    std::vector<std::filesystem::path> roots;
    if (!importing_file.empty()) roots.emplace_back(std::filesystem::path(importing_file).parent_path());
    for (const auto& path : search_paths) roots.emplace_back(path);

    for (const auto& root : roots) {
        const auto candidate = (root / (relative_module + ".pycpp")).lexically_normal();
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec)) {
            const auto canonical = std::filesystem::weakly_canonical(candidate, ec);
            return (ec ? candidate : canonical).string();
        }
        // Also accept package-style modules: package/__init__.pycpp.
        const auto package_candidate = (root / relative_module / "__init__.pycpp").lexically_normal();
        ec.clear();
        if (std::filesystem::is_regular_file(package_candidate, ec)) {
            const auto canonical = std::filesystem::weakly_canonical(package_candidate, ec);
            return (ec ? package_candidate : canonical).string();
        }
    }
    return {};
}
