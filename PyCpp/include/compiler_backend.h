#ifndef COMPILER_BACKEND_H
#define COMPILER_BACKEND_H

#include <functional>
#include <string>
#include <vector>

// The C++ compiler used to build the C++ that PyCPP generates.
enum class CompilerKind {
    Auto, // pick a sensible default for the current platform
    Gcc,  // g++ (GCC, or MinGW on Windows)
    Msvc  // cl.exe from Visual Studio (Windows only)
};

// Everything a backend needs to turn generated .cpp files into an executable.
struct CompileRequest {
    std::vector<std::string> cppFiles; // generated implementation files (all modules)
    std::string exeFile;               // output executable path
    std::string includeDir;            // directory holding the generated headers
};

class CompilerBackend {
public:
    // Accepts "gcc", "g++", "msvc", "cl" or "auto" (case-insensitive).
    // Returns false and leaves `out` untouched for anything else.
    static bool parseKind(const std::string& name, CompilerKind& out);

    // Auto -> MSVC on Windows (the historical default), gcc everywhere else.
    static CompilerKind resolve(CompilerKind requested);

    static std::string displayName(CompilerKind kind);

    // Extension of the executable the compiler produces ("" or ".exe").
    static std::string exeExtension(CompilerKind kind);

    // Pure functions: build the text that would be executed. Kept free of side
    // effects so they can be unit-tested on any platform.
    static std::string buildCompileCommand(CompilerKind kind, const CompileRequest& request);
    static std::string buildMsvcBatch(const CompileRequest& request);

    // Is the compiler usable right now? On failure `reason` explains why.
    static bool isAvailable(CompilerKind kind, std::string& reason);

    // Compile the request. Compiler diagnostics are passed to `onOutput` line by
    // line. Returns true when an executable was produced.
    static bool compile(CompilerKind kind, const CompileRequest& request, bool verbose,
        const std::function<void(const std::string&)>& onOutput);

    // Run a finished executable, passing its output to `onOutput` line by line.
    // Returns the program's exit code, or -1 if it could not be started.
    static int run(const std::string& exeFile,
        const std::function<void(const std::string&)>& onOutput);
};

#endif // COMPILER_BACKEND_H
