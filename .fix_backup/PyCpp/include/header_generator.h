#ifndef HEADER_GENERATOR_H
#define HEADER_GENERATOR_H

#include "../include/ast.h"
#include <sstream>
#include <string>
#include <vector>
#include <unordered_set>

class HeaderGenerator {
private:
    std::ostringstream output;
    int indent_level;

    void generateIncludeGuard(const std::string& filename);
    void generateType(Type* type);
    void generateDefaultValue(Expr* expr);
    std::string getBaseFilename(const std::string& filename);

public:
    HeaderGenerator();
    std::string generate(std::unique_ptr<Program>& program,
        const std::string& filename,
        const std::vector<std::string>& manual_includes = {});
};

#endif // HEADER_GENERATOR_H