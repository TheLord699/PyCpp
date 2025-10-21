#ifndef INCLUDE_MANAGER_H
#define INCLUDE_MANAGER_H

#include <unordered_set>
#include <vector>
#include <string>
#include <unordered_map>

class IncludeManager {
private:
    std::unordered_set<std::string> used_features;
    std::unordered_set<std::string> manual_includes;

    std::unordered_map<std::string, std::vector<std::string>> feature_headers = {
        // Core C++ headers
        {"iostream", {"<iostream>"}},
        {"vector", {"<vector>"}},
        {"string", {"<string>"}},
        {"map", {"<map>"}},
        {"set", {"<set>"}},
        {"tuple", {"<tuple>"}},
        {"algorithm", {"<algorithm>"}},
        {"cmath", {"<cmath>"}},
        {"numeric", {"<numeric>"}},
        {"cstdlib", {"<cstdlib>"}},
        {"ctime", {"<ctime>"}},
        {"sstream", {"<sstream>"}},
        {"iomanip", {"<iomanip>"}},
        {"functional", {"<functional>"}},
        {"memory", {"<memory>"}},
        {"utility", {"<utility>"}},
        {"random", {"<random>"}},
        {"typeinfo", {"<typeinfo>"}},
        {"type_traits", {"<type_traits>"}},

        // Input/Output
        {"print", {"<iostream>"}},
        {"input", {"<iostream>", "<string>"}},

        // Type conversion functions
        {"int", {"<string>"}},        // For std::stoi
        {"float", {"<string>"}},      // For std::stof  
        {"str", {"<string>"}},
        {"bool", {}},
        {"list", {"<vector>"}},
        {"dict", {"<map>"}},
        {"tuple", {"<tuple>"}},
        {"set", {"<set>"}},

        // Math functions
        {"abs", {"<cmath>"}},
        {"pow", {"<cmath>"}},
        {"round", {"<cmath>"}},
        {"sqrt", {"<cmath>"}},
        {"sin", {"<cmath>"}},
        {"cos", {"<cmath>"}},
        {"tan", {"<cmath>"}},
        {"log", {"<cmath>"}},
        {"log10", {"<cmath>"}},
        {"exp", {"<cmath>"}},
        {"ceil", {"<cmath>"}},
        {"floor", {"<cmath>"}},

        // Collection functions
        {"len", {"<vector>", "<map>", "<set>", "<string>"}},
        {"min", {"<algorithm>"}},
        {"max", {"<algorithm>"}},
        {"sum", {"<numeric>"}},
        {"range", {"<vector>"}},
        {"sorted", {"<algorithm>"}},
        {"reversed", {"<algorithm>"}},
        {"enumerate", {"<vector>", "<utility>"}},
        {"zip", {"<vector>", "<tuple>"}},
        {"map_func", {"<vector>", "<functional>"}},
        {"filter", {"<vector>", "<functional>"}},
        {"reduce", {"<numeric>"}},

        // String conversion functions
        {"bin", {"<sstream>", "<iomanip>"}},
        {"oct", {"<sstream>", "<iomanip>"}},
        {"hex", {"<sstream>", "<iomanip>"}},
        {"chr", {}},
        {"ord", {}},
        {"format", {"<string>"}},
        {"join", {"<string>"}},
        {"split", {"<string>", "<sstream>"}},
        {"strip", {"<string>", "<algorithm>"}},
        {"replace", {"<string>"}},
        {"find", {"<string>"}},
        {"startswith", {"<string>"}},
        {"endswith", {"<string>"}},
        {"lower", {"<string>", "<algorithm>"}},
        {"upper", {"<string>", "<algorithm>"}},
        {"capitalize", {"<string>", "<algorithm>"}},
        {"title", {"<string>", "<algorithm>"}},

        // File operations
        {"open", {"<fstream>"}},
        {"read", {"<fstream>"}},
        {"write", {"<fstream>"}},
        {"close", {"<fstream>"}},
        {"seek", {"<fstream>"}},
        {"tell", {"<fstream>"}},

        // Random functions
        {"random", {"<random>", "<ctime>"}},
        {"randint", {"<random>", "<ctime>"}},
        {"choice", {"<random>", "<ctime>"}},
        {"shuffle", {"<algorithm>", "<random>", "<ctime>"}},
        {"uniform", {"<random>", "<ctime>"}},

        // System functions
        {"exit", {"<cstdlib>"}},
        {"quit", {"<cstdlib>"}},
        {"help", {}},
        {"dir", {"<typeinfo>"}},
        {"vars", {}},
        {"locals", {}},
        {"globals", {}},
        {"id", {"<memory>"}},
        {"hash", {"<functional>"}},

        // Memory management
        {"del", {}},
        {"gc", {}},
        {"malloc", {"<cstdlib>"}},
        {"free", {"<cstdlib>"}},
        {"mem.alloc", {"<cstdlib>"}},
        {"mem.free", {"<cstdlib>"}},
        {"mem", {"<cstdlib>"}},

        // Type checking
        {"type_func", {"<typeinfo>"}},
        {"isinstance", {"<typeinfo>"}},
        {"issubclass", {"<typeinfo>"}},
        {"hasattr", {}},
        {"getattr", {}},
        {"setattr", {}},
        {"delattr", {}},

        // Math constants and modules
        {"math", {"<cmath>"}},

        // Divmod function
        {"divmod", {"<utility>"}},

        // Compound operators (no specific header needed)
        {"compound_ops", {}},

        // Special methods (no specific headers)
        {"init", {}},
        {"str_special", {}},
        {"repr", {}},
        {"len_special", {}},
        {"getitem", {}},
        {"setitem", {}},
        {"delitem", {}},
        {"iter", {}},
        {"next", {}},
        {"call", {}},
        {"add_special", {}},
        {"sub_special", {}},
        {"mul_special", {}},
        {"truediv_special", {}},
        {"floordiv_special", {}},
        {"mod_special", {}},
        {"pow_special", {}},
        {"eq_special", {}},
        {"ne_special", {}},
        {"lt_special", {}},
        {"le_special", {}},
        {"gt_special", {}},
        {"ge_special", {}},

        // Exception handling
        {"try", {}},
        {"except", {}},
        {"finally", {}},
        {"raise", {}},
        {"assert", {"<cassert>"}},

        // Advanced features
        {"global", {}},
        {"nonlocal", {}},
        {"exec", {}},
        {"eval", {}},
        {"compile", {}},

        // Decorators
        {"staticmethod", {}},
        {"classmethod", {}},
        {"property", {}},

        // Context managers
        {"with", {}},
        {"open", {"<fstream>"}},

        // Async features
        {"async", {}},
        {"await", {}},
        {"yield", {}},

        // Lambda
        {"lambda", {"<functional>"}}
    };

public:
    IncludeManager();
    void trackFeature(const std::string& feature);
    void trackManualInclude(const std::string& header);
    std::vector<std::string> getRequiredIncludes() const;
    bool isHeaderIncluded(const std::string& header) const;
};

#endif // INCLUDE_MANAGER_H