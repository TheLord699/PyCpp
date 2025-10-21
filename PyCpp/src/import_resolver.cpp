#include "import_resolver.h"
#include <fstream>
#include <iostream>

ImportResolver::ImportResolver() {
    search_paths.push_back(".");
    search_paths.push_back("./lib");
    search_paths.push_back("./src");
}

void ImportResolver::addSearchPath(const std::string& path) {
    search_paths.push_back(path);
}

std::vector<std::string> ImportResolver::resolveAllDependencies(const std::string& entry_file) {
    std::vector<std::string> all_files;
    processed_files.clear();
    resolveDependenciesRecursive(entry_file, all_files);
    return all_files;
}

void ImportResolver::resolveDependenciesRecursive(const std::string& filename, std::vector<std::string>& result) {
    if (processed_files.count(filename)) {
        return; // Already processed
    }

    processed_files.insert(filename);
    result.push_back(filename);

    // Extract imports from this file
    auto imports = extractImports(filename);

    // Resolve each import recursively
    for (const auto& module_name : imports) {
        std::string module_file = findModuleFile(module_name);
        if (!module_file.empty()) {
            resolveDependenciesRecursive(module_file, result);
        }
    }
}

std::vector<std::string> ImportResolver::extractImports(const std::string& filename) {
    std::vector<std::string> imports;
    std::ifstream file(filename);
    std::string line;

    while (std::getline(file, line)) {
        // Look for "import module" or "from module import ..."
        if (line.find("import ") != std::string::npos || line.find("from ") != std::string::npos) {
            // Simple parsing - extract module name
            if (line.find("from ") == 0) {
                // from module import x, y
                size_t from_pos = 5; // after "from "
                size_t import_pos = line.find(" import ");
                if (import_pos != std::string::npos) {
                    std::string module = line.substr(from_pos, import_pos - from_pos);
                    // Trim whitespace
                    module.erase(0, module.find_first_not_of(" \t"));
                    module.erase(module.find_last_not_of(" \t") + 1);
                    imports.push_back(module);
                }
            }
            else if (line.find("import ") == 0) {
                // import module
                size_t import_pos = 7; // after "import "
                std::string module = line.substr(import_pos);
                // Trim whitespace and remove any trailing comments
                module.erase(0, module.find_first_not_of(" \t"));
                size_t comment_pos = module.find('#');
                if (comment_pos != std::string::npos) {
                    module = module.substr(0, comment_pos);
                }
                module.erase(module.find_last_not_of(" \t") + 1);
                imports.push_back(module);
            }
        }
    }

    return imports;
}

std::string ImportResolver::findModuleFile(const std::string& module_name) {
    for (const auto& path : search_paths) {
        std::string full_path = path + "/" + module_name + ".pycpp";
        if (std::filesystem::exists(full_path)) {
            return full_path;
        }
    }
    return "";
}