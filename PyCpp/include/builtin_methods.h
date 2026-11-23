#ifndef PYCPP_BUILTIN_METHODS_H
#define PYCPP_BUILTIN_METHODS_H
// Python methods of str, list, dict and set, and what they return.
//
// The analyzer uses this table to check calls (`name.upper(1)` is an error) and to know
// result types (`words = line.split()` is a list[str]). The code generator turns a call of
// one of these methods into a call of the runtime helper `pycpp::py_<name>(object, args...)`
// (pycpp_runtime.h), which gives it Python semantics on std::string, std::vector, std::map
// and std::set, and simply calls the member function on any other type (a user class that
// has its own `pop` keeps it).

#include <cstring>
#include <string>
#include <vector>

namespace pycpp {

enum class Receiver { Str, List, Dict, Set };

struct BuiltinMethod {
    Receiver receiver;
    const char* name;
    int min_args;
    int max_args;
    // Result: "str", "int", "bool", "void", "same" (the receiver's type), "elem" (list/set
    // element), "key", "value", "list[str]", "list[key]", "list[value]", "list[item]"
    const char* result;
};

inline const std::vector<BuiltinMethod>& builtinMethods() {
    static const std::vector<BuiltinMethod> table = {
        // str
        {Receiver::Str, "upper", 0, 0, "str"},        {Receiver::Str, "lower", 0, 0, "str"},
        {Receiver::Str, "strip", 0, 1, "str"},        {Receiver::Str, "lstrip", 0, 1, "str"},
        {Receiver::Str, "rstrip", 0, 1, "str"},       {Receiver::Str, "split", 0, 2, "list[str]"},
        {Receiver::Str, "rsplit", 0, 2, "list[str]"}, {Receiver::Str, "splitlines", 0, 0, "list[str]"},
        {Receiver::Str, "join", 1, 1, "str"},         {Receiver::Str, "replace", 2, 3, "str"},
        {Receiver::Str, "find", 1, 1, "int"},         {Receiver::Str, "rfind", 1, 1, "int"},
        {Receiver::Str, "index", 1, 1, "int"},        {Receiver::Str, "count", 1, 1, "int"},
        {Receiver::Str, "startswith", 1, 1, "bool"},  {Receiver::Str, "endswith", 1, 1, "bool"},
        {Receiver::Str, "isdigit", 0, 0, "bool"},     {Receiver::Str, "isalpha", 0, 0, "bool"},
        {Receiver::Str, "isalnum", 0, 0, "bool"},     {Receiver::Str, "isspace", 0, 0, "bool"},
        {Receiver::Str, "isupper", 0, 0, "bool"},     {Receiver::Str, "islower", 0, 0, "bool"},
        {Receiver::Str, "title", 0, 0, "str"},        {Receiver::Str, "capitalize", 0, 0, "str"},
        {Receiver::Str, "swapcase", 0, 0, "str"},     {Receiver::Str, "center", 1, 2, "str"},
        {Receiver::Str, "ljust", 1, 2, "str"},        {Receiver::Str, "rjust", 1, 2, "str"},
        {Receiver::Str, "zfill", 1, 1, "str"},
        // list
        {Receiver::List, "append", 1, 1, "void"},     {Receiver::List, "extend", 1, 1, "void"},
        {Receiver::List, "insert", 2, 2, "void"},     {Receiver::List, "pop", 0, 1, "elem"},
        {Receiver::List, "remove", 1, 1, "void"},     {Receiver::List, "index", 1, 1, "int"},
        {Receiver::List, "count", 1, 1, "int"},       {Receiver::List, "clear", 0, 0, "void"},
        {Receiver::List, "sort", 0, 0, "void"},       {Receiver::List, "reverse", 0, 0, "void"},
        {Receiver::List, "copy", 0, 0, "same"},
        // dict
        {Receiver::Dict, "get", 1, 2, "value"},       {Receiver::Dict, "keys", 0, 0, "list[key]"},
        {Receiver::Dict, "values", 0, 0, "list[value]"}, {Receiver::Dict, "items", 0, 0, "list[item]"},
        {Receiver::Dict, "pop", 1, 2, "value"},       {Receiver::Dict, "update", 1, 1, "void"},
        {Receiver::Dict, "clear", 0, 0, "void"},      {Receiver::Dict, "copy", 0, 0, "same"},
        {Receiver::Dict, "setdefault", 2, 2, "value"},
        // set
        {Receiver::Set, "add", 1, 1, "void"},         {Receiver::Set, "remove", 1, 1, "void"},
        {Receiver::Set, "discard", 1, 1, "void"},     {Receiver::Set, "clear", 0, 0, "void"},
        {Receiver::Set, "copy", 0, 0, "same"},
    };
    return table;
}

inline const BuiltinMethod* findBuiltinMethod(Receiver r, const std::string& name) {
    for (const auto& m : builtinMethods())
        if (m.receiver == r && name == m.name) return &m;
    return nullptr;
}

// Is `name` a method of any built-in type? (the code generator routes those through the runtime)
inline bool isBuiltinMethodName(const std::string& name) {
    for (const auto& m : builtinMethods())
        if (name == m.name) return true;
    return false;
}

inline const char* receiverName(Receiver r) {
    switch (r) {
    case Receiver::Str: return "str";
    case Receiver::List: return "list";
    case Receiver::Dict: return "dict";
    default: return "set";
    }
}

} // namespace pycpp
#endif
