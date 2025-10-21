#ifndef IMPORT_RESOLVER_H
#define IMPORT_RESOLVER_H

#include <string>
#include <vector>
#include <unordered_set>
#include <filesystem>

class ImportResolver {
private:
    std::vector<std::string> search_paths;
    std::unordered_set<std::string> processed_files;

public:
    ImportResolver();

    void addSearchPath(const std::string& path);
    std::vector<std::string> resolveAllDependencies(const std::string& entry_file);
    std::string findModuleFile(const std::string& module_name);

private:
    void resolveDependenciesRecursive(const std::string& filename, std::vector<std::string>& result);
    std::vector<std::string> extractImports(const std::string& filename);
};

#endif