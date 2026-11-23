#ifndef COMPILER_BACKEND_H
#define COMPILER_BACKEND_H

#include <functional>
#include <string>
#include <vector>

// The C++ compiler used to build the C++ that PyCPP generates.
enum class CompilerKind {
    Auto, // pick a sensible default for the current platform
    Gcc,  // g++ (GCC, or MinGW on Windows)
    Msvc, // cl.exe from Visual Studio (Windows only)
    Clang // clang++ (macOS and other supported platforms)
};

// Everything a backend needs to turn generated .cpp files into an executable.
struct CompileRequest {
    std::vector<std::string> cppFiles; // generated implementation files (all modules)
    std::string exeFile;               // output executable path
    std::string includeDir;            // directory holding the generated headers
    std::string linkerFlags;
    bool optimize = false;         // -O2 / /O2 (used by --build)
};

// One translation unit of an incremental build.
struct BuildUnit {
    std::string cppFile;     // generated implementation file
    std::string objFile;     // object file it compiles to (may be reused from the cache)
    bool compile = true;     // false: objFile is up to date
};

// An incremental, parallel build: compile the units that need it, then link everything.
struct BuildRequest {
    std::vector<BuildUnit> units;
    std::string exeFile;
    std::string includeDir;
    std::string linkerFlags;
    bool optimize = false;
    unsigned jobs = 1;       // compiler processes run at the same time (g++/clang++)
};

class CompilerBackend {
public:
    // Accepts "gcc", "g++", "clang", "clang++", "msvc", "cl" or "auto" (case-insensitive).
    // Returns false and leaves `out` untouched for anything else.
    static bool parseKind(const std::string& name, CompilerKind& out);

    // Auto -> MSVC on Windows, Clang on macOS, GCC elsewhere.
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

    // Commands of an incremental build (pure, for tests and --verbose)
    static std::string buildObjectCommand(CompilerKind kind, const std::string& cppFile, const std::string& objFile,
        const std::string& includeDir, bool optimize);
    static std::string buildLinkCommand(CompilerKind kind, const std::vector<std::string>& objFiles,
        const std::string& exeFile, const std::string& linkerFlags);
    static std::string objectExtension(CompilerKind kind);   // ".o" or ".obj"

    // Compile the units that need it (in parallel) and link. `onOutput` receives the
    // compiler's output, kept together per unit. `compiled` reports, per unit, whether
    // a fresh object file was produced. Returns true when the executable was produced.
    static bool build(CompilerKind kind, const BuildRequest& request, bool verbose,
        const std::function<void(const std::string&)>& onOutput, std::vector<bool>& compiled);

    // Run a finished executable, passing its output to `onOutput` line by line.
    // Returns the program's exit code, or -1 if it could not be started.
    static int run(const std::string& exeFile,
        const std::function<void(const std::string&)>& onOutput);
};

#endif // COMPILER_BACKEND_H
