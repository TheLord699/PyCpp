#include <cctype>
#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>
#include <map>
#include <set>
#include <iomanip>

// Use only the specific Windows functionality we need, avoid windows.h
#ifdef _WIN32
#include <io.h>
#include <process.h>
#define popen _popen
#define pclose _pclose
#define access _access
#define F_OK 0
#endif

#include "../include/lexer.h"
#include "../include/pycpp_parser.h"
#include "../include/semantic_analyzer.h"
#include "../include/include_manager.h"
#include "../include/code_generator.h"
#include "../include/header_generator.h"
#include "../include/import_resolver.h"
#include "../include/compiler_backend.h"
#include "../include/log.h"
#include "../include/pycpp_runtime_text.h"
#include "../include/diagnostics.h"

// Terminal output formatting
class TerminalFormatter {
    // Progress and status lines appear only with -v / --verbose.
    static void status(const std::string& message) {
        if (pycpp::verboseFlag()) std::cout << "   " << message << std::endl;
    }

public:
    static void printInfo(const std::string& message) { status(message); }
    static void printSuccess(const std::string& message) { status(message); }
    static void printFile(const std::string& message) { status(message); }
    static void printCompile(const std::string& message) { status(message); }
    static void printRun(const std::string& message) { status(message); }
    static void printSubsection(const std::string& message) { status(message); }
    static void printSettingUp() { status("Setting up Visual Studio environment..."); }
    static void printCompiling(const std::string& filename) { status("Compiling " + filename + "..."); }
    static void printFilename(const std::string& filename) { status(filename); }
    static void printRunning(const std::string& filename) { status("Running " + filename + "..."); }

    static void printSection(const std::string& message) {
        if (!pycpp::verboseFlag()) return;
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << " " << message << std::endl;
        std::cout << std::string(60, '=') << std::endl;
    }

    // The outcome of a command the user explicitly asked for (e.g. --no-compile).
    static void printResult(const std::string& message) {
        std::cout << message << std::endl;
    }

    static void printNote(const std::string& message) {
        std::cerr << "note: " << message << std::endl;
    }

    // Warnings and errors are always shown, on stderr.
    static void printWarning(const std::string& message) {
        std::cerr << "warning: " << message << std::endl;
    }

    static void printError(const std::string& message) {
        std::cerr << "error: " << message << std::endl;
    }

    static void printProgramOutput(const std::string& output) {
        std::cout << output;
    }

    // Compiler diagnostics (g++/cl output) are always shown.
    static void printCompilerOutput(const std::string& output) {
        if (!output.empty() && output != "\n") {
            std::cerr << output;
        }
    }

    // The compiled program's own output: untouched, so it can be piped.
    static void printProgramOutputLine(const std::string& line) {
        std::cout << line << std::endl;
    }

    static void printExitCode(int code) {
        if (pycpp::verboseFlag()) {
            std::cout << "   PROGRAM_EXIT_CODE:" << code << std::endl;
        }
        else if (code != 0) {
            std::cerr << "Program exited with code " << code << std::endl;
        }
    }
};

// Render a failure: structured errors get a code frame, everything else a plain line.
static void reportFailure(const std::exception& e) {
    if (auto ce = dynamic_cast<const pycpp::CompileError*>(&e)) {
        for (const auto& d : ce->diagnostics) pycpp::emit(d);
        const size_t n = ce->diagnostics.size();
        std::cerr << "error: could not compile due to " << n << (n == 1 ? " error" : " errors") << "\n";
    }
    else {
        std::cerr << "error: " << e.what() << "\n";
    }
}

// Helper function to clean up .obj files in a specific directory
void cleanupObjectFiles(const std::filesystem::path& directory, bool verbose = false) {
    try {
        if (!std::filesystem::exists(directory)) {
            return;
        }

        int objCount = 0;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".obj") {
                std::filesystem::remove(entry.path());
                objCount++;
                if (verbose) {
                    TerminalFormatter::printInfo("Removed object file: " + entry.path().filename().string());
                }
            }
        }
        if (verbose && objCount > 0) {
            TerminalFormatter::printInfo("Removed " + std::to_string(objCount) + " object file(s) from " + directory.string());
        }
    }
    catch (const std::filesystem::filesystem_error& e) {
        if (verbose) {
            TerminalFormatter::printWarning("Could not clean up object files in " + directory.string() + ": " + e.what());
        }
    }
}

// Helper function to clean up .obj files in current directory
void cleanupStrayObjectFiles(bool verbose = false) {
    try {
        int objCount = 0;
        for (const auto& entry : std::filesystem::directory_iterator(".")) {
            if (entry.is_regular_file() && entry.path().extension() == ".obj") {
                std::filesystem::remove(entry.path());
                objCount++;
                if (verbose) {
                    TerminalFormatter::printInfo("Removed stray object file: " + entry.path().filename().string());
                }
            }
        }
        if (verbose && objCount > 0) {
            TerminalFormatter::printInfo("Removed " + std::to_string(objCount) + " stray object file(s) from current directory");
        }
    }
    catch (const std::exception& e) {
        // Ignore cleanup errors for stray files
    }
}

void generateHeaderAndImplementation(std::unique_ptr<Program>& ast,
    const std::string& base_filename,
    IncludeManager& include_manager) {
    TerminalFormatter::printSubsection("Generating Header and Implementation Files");

    // Extract manual includes from use statements
    std::vector<std::string> manual_includes;
    for (auto& stmt : ast->statements) {
        if (auto use_stmt = dynamic_cast<UseStmt*>(stmt.get())) {
            manual_includes.push_back("<" + use_stmt->library + ">");
        }
    }

    // Generate the header: class/struct/template/constexpr definitions plus
    // prototypes of every function the source file defines.
    std::string guard_name = std::filesystem::path(base_filename).filename().string();
    for (char& c : guard_name) c = std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : '_';
    guard_name = "PYCPP_" + guard_name + "_H";

    // Every module's declarations live in their own namespace, named after its generated
    // file; importing modules use the same name (ImportStmt::ns).
    std::string module_ns = std::filesystem::path(base_filename).filename().string();
    for (char& c : module_ns) if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
    module_ns = "pycpp_" + module_ns;

    CodeGenerator header_gen;
    header_gen.setNamespace(module_ns);
    std::string header_content = header_gen.generateHeader(ast, include_manager, guard_name);

    std::string header_filename = base_filename + ".h";
    std::ofstream header_file(header_filename);
    if (!header_file) {
        throw std::runtime_error("Cannot open header file for writing: " + header_filename);
    }
    header_file << header_content;
    header_file.close();

    TerminalFormatter::printFile("Generated header: " + header_filename);

    // Generate implementation file with ONLY function implementations
    CodeGenerator code_gen;
    code_gen.setNamespace(module_ns);
    include_manager.trackManualInclude("\"" + base_filename + ".h\"");

    std::string cpp_content = code_gen.generate(ast, include_manager);

    std::string cpp_filename = base_filename + ".cpp";
    std::ofstream cpp_file(cpp_filename);
    if (!cpp_file) {
        throw std::runtime_error("Cannot open implementation file for writing: " + cpp_filename);
    }
    cpp_file << cpp_content;
    cpp_file.close();

    TerminalFormatter::printFile("Generated implementation: " + cpp_filename);

    if (include_manager.isHeaderIncluded("\"pycpp_runtime.h\"")) {
        writeRuntimeHeader(std::filesystem::path(base_filename).parent_path());
    }
    TerminalFormatter::printSuccess("Total files generated: 2");
}

// Read, lex, parse and analyze one module.
std::unique_ptr<Program> loadModule(const std::string& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open '" + file + "'");
    std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (src.size() >= 3 && (unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF)
        src.erase(0, 3);
    pycpp::setSource(file, src);            // for the code frames in diagnostics
    Lexer lexer(src);
    PyCppParser parser(lexer.tokenize());
    auto ast = parser.parse();
    SemanticAnalyzer analyzer;
    analyzer.analyze(ast);
    return ast;
}

// Name (without extension) of the generated .h/.cpp pair of every module.
// It is the module's path relative to the entry file's folder, with separators turned
// into '_':   graphics/shapes.pycpp   -> graphics_shapes
//             graphics/__init__.pycpp -> graphics        (a package is named after its folder)
// so modules with the same file name in different folders never share a generated name.
static std::map<std::string, std::string> computeModuleStems(const std::vector<std::string>& files) {
    namespace fs = std::filesystem;
    std::map<std::string, std::string> stems;
    if (files.empty()) return stems;

    const fs::path root = fs::path(files.front()).parent_path();   // files.front() is the entry file
    std::map<std::string, std::string> owner;                      // generated name -> source file

    auto sanitize = [](std::string s) {
        for (char& c : s)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') c = '_';
        return s;
    };

    for (const auto& file : files) {
        const fs::path p(file);
        fs::path rel = p.lexically_relative(root);
        if (rel.empty() || rel.begin()->string() == "..") {
            // Outside the entry folder (-I path, ./lib): keep the parent folder's name
            rel = fs::path(p.parent_path().filename()) / p.filename();
        }

        std::vector<std::string> parts;
        for (const auto& part : rel.parent_path()) parts.push_back(part.string());
        const std::string leaf = rel.stem().string();
        if (leaf != "__init__" || parts.empty()) parts.push_back(leaf);

        std::string stem;
        for (const auto& part : parts) stem += (stem.empty() ? "" : "_") + part;
        stem = sanitize(stem);

        auto inserted = owner.emplace(stem, file);
        if (!inserted.second && inserted.first->second != file)
            throw std::runtime_error("modules '" + inserted.first->second + "' and '" + file +
                "' would both generate '" + stem + ".h'; rename one of them");
        stems[file] = stem;
    }
    return stems;
}

// Generate multiple files for multi-file compilation.
// All modules are parsed and registered first, so a class can see the members of a
// base class that lives in another module.
void generateMultiFileOutput(const std::vector<std::string>& files, const std::string& tempDir, bool verbose,
    ImportResolver& resolver, const std::map<std::string, std::string>& stems) {
    std::vector<std::pair<std::string, std::unique_ptr<Program>>> modules;
    for (const auto& file : files) {
        if (verbose) TerminalFormatter::printInfo("Processing: " + file);
        auto ast = loadModule(file);
        pycpp::registerClasses(ast.get());
        modules.emplace_back(file, std::move(ast));
    }

    // Point every import at the header (and namespace) its target file generates, and
    // find the imports that are part of an import cycle
    std::map<std::string, std::vector<std::string>> graph;   // file -> files it imports
    for (auto& [file, ast] : modules) {
        for (auto& stmt : ast->statements) {
            auto import_stmt = dynamic_cast<ImportStmt*>(stmt.get());
            if (!import_stmt) continue;
            const std::string target = resolver.findModuleFile(import_stmt->module, file);
            auto it = stems.find(target);
            if (it != stems.end()) import_stmt->header_stem = it->second;
            if (!target.empty()) graph[file].push_back(target);
        }
    }
    auto reaches = [&graph](const std::string& from, const std::string& goal) {
        std::set<std::string> seen;
        std::vector<std::string> stack{ from };
        while (!stack.empty()) {
            const std::string current = stack.back();
            stack.pop_back();
            if (current == goal) return true;
            if (!seen.insert(current).second) continue;
            for (const auto& next : graph[current]) stack.push_back(next);
        }
        return false;
    };
    for (auto& [file, ast] : modules) {
        for (auto& stmt : ast->statements) {
            auto import_stmt = dynamic_cast<ImportStmt*>(stmt.get());
            if (!import_stmt) continue;
            const std::string target = resolver.findModuleFile(import_stmt->module, file);
            import_stmt->cyclic = !target.empty() && reaches(target, file);
        }
    }

    for (auto& [file, ast] : modules) {
        IncludeManager include_manager;
        generateHeaderAndImplementation(ast,
            (std::filesystem::path(tempDir) / stems.at(file)).string(), include_manager);
    }
}

void printUsage(const std::string& programName) {
    std::cout << "PyCPP Compiler - Compile PyCPP code to C++\n\n";
    std::cout << "Usage: " << programName << " <input_file.pycpp> [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -o <output>      Specify output base name (without extension)\n";
    std::cout << "  -h, --help       Show this help message\n";
    std::cout << "  -v, --verbose    Enable verbose output\n";
    std::cout << "  --keep-temp      Keep temporary compilation files\n";
    std::cout << "  -b, --build      Build an optimized executable next to the source, don't run it\n";
    std::cout << "  --no-compile     Only generate .h and .cpp files, don't compile\n";
    std::cout << "  --single-file    Compile only the specified file (no auto-deps)\n";
    std::cout << "  --compiler <c>   C++ compiler for the generated code: gcc, msvc or auto\n";
    std::cout << "                   (default: auto = msvc on Windows, gcc elsewhere)\n";
    std::cout << "  --linker-flags <flags>  Extra flags passed to the linker (e.g. \"-lraylib -lopengl32\")\n"; // --- ADDED ---
    std::cout << "  -I <path>        Add directory to import search path\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << programName << " program.pycpp                          # Multi-file compilation with auto-deps\n";
    std::cout << "  " << programName << " program.pycpp -o myprogram            # Custom output name\n";
    std::cout << "  " << programName << " program.pycpp -v                      # Verbose output\n";
    std::cout << "  " << programName << " program.pycpp --keep-temp             # Keep temporary files\n";
    std::cout << "  " << programName << " program.pycpp --no-compile            # Generate C++ only\n";
    std::cout << "  " << programName << " program.pycpp --single-file           # Compile only one file\n";
    std::cout << "  " << programName << " program.pycpp -I ./lib               # With custom import path\n";
    std::cout << "  " << programName << " program.pycpp --compiler gcc          # Build with g++\n";
    std::cout << "  " << programName << " program.pycpp --compiler msvc         # Build with Visual Studio (cl.exe)\n";
    std::cout << "  " << programName << " game.pycpp --linker-flags \"-lraylib -lopengl32 -lgdi32 -lwinmm\"  # Link external libraries\n"; // --- ADDED ---
}

// Turn raw C++ compiler output into one short PyCPP diagnostic. The C++ output itself
// is only shown with --verbose.
static void explainBuildFailure(const std::vector<std::string>& log, const std::string& tempDir) {
    pycpp::Diagnostic d;
    for (const auto& line : log) {
        size_t p;
        if ((p = line.find("undefined reference to `")) != std::string::npos) {
            size_t s = p + 24, e = line.find('\'', s);
            d.message = "linking failed: cannot find '" + line.substr(s, e == std::string::npos ? std::string::npos : e - s) + "'";
        }
        else if ((p = line.find("unresolved external symbol ")) != std::string::npos) {
            size_t s = p + 27, e = line.find(" referenced", s);
            d.message = "linking failed: cannot find " + line.substr(s, e == std::string::npos ? std::string::npos : e - s);
        }
        if (!d.message.empty()) {
            d.hint = "the library that defines it isn't linked, pass it with --linker-flags \"-l<name>\"";
            break;
        }
        if ((p = line.find("fatal error: ")) != std::string::npos && line.find("No such file") != std::string::npos) {
            size_t s = p + 13;
            d.message = "cannot find C++ header '" + line.substr(s, line.find(':', s) - s) + "'";
            d.hint = "check the name in your `use <...>` line and that the library is installed";
            break;
        }
        if ((p = line.find("C1083")) != std::string::npos) {
            d.message = "cannot find a C++ header: " + line.substr(p + 7);
            d.hint = "check your `use <...>` lines and that the library is installed";
            break;
        }
    }
    if (d.message.empty()) {
        d.message = "the generated C++ failed to compile";
        d.hint = "this is a PyCPP bug or an unsupported construct. Re-run with --verbose to see the "
                 "C++ compiler output, or --keep-temp to inspect " + tempDir;
    }
    pycpp::emit(d);
}

// Exit code of the program that was built and run; becomes pycpp's own exit code.
static int g_programExitCode = 0;

// Compile the generated C++ with the selected compiler, then run the result.
// Returns true when the program was built and started.
bool buildAndRun(CompilerKind compiler, const std::vector<std::string>& cppFiles,
    const std::string& exeFile, const std::string& includeDir, bool verbose,
    const std::string& linkerFlags, bool buildOnly = false) {
    std::string reason;
    if (!CompilerBackend::isAvailable(compiler, reason)) {
        TerminalFormatter::printError(reason);
        return false;
    }

    CompileRequest request;
    request.cppFiles = cppFiles;
    request.exeFile = exeFile;
    request.includeDir = includeDir;
    request.linkerFlags = linkerFlags; // --- ADDED ---
    request.optimize = buildOnly;

    TerminalFormatter::printCompile("Compiling with " + CompilerBackend::displayName(compiler) + "...");
    for (const auto& file : cppFiles) {
        TerminalFormatter::printFilename(std::filesystem::path(file).filename().string());
    }

    std::vector<std::string> log;
    bool built = CompilerBackend::compile(compiler, request, verbose, [&](const std::string& line) {
        log.push_back(line);
        if (verbose && !line.empty()) TerminalFormatter::printCompilerOutput(line + "\n");
    });
    cleanupStrayObjectFiles(verbose);

    if (!built) {
        if (!verbose) explainBuildFailure(log, includeDir);   // never dump raw C++ errors
        TerminalFormatter::printError("Compilation failed");
        return false;
    }

    if (buildOnly) {
        TerminalFormatter::printResult("Built " + exeFile);
        return true;
    }

    TerminalFormatter::printRunning(exeFile);
    if (pycpp::verboseFlag()) std::cout << std::endl;
    int exitCode = CompilerBackend::run(exeFile, [](const std::string& line) {
        TerminalFormatter::printProgramOutputLine(line);
    });
    if (exitCode < 0) {
        TerminalFormatter::printError("Could not start the compiled program");
        return false;
    }
    TerminalFormatter::printExitCode(exitCode);
    g_programExitCode = exitCode;
    return true;
}

// Multi-file compilation with automatic dependency resolution (DEFAULT)
bool compileMultiFile(const std::string& inputFile, const std::string& outputBase,
    const std::vector<std::string>& importPaths, bool verbose, bool keepTemp, bool noCompile,
    CompilerKind compiler, const std::string& linkerFlags, bool buildOnly = false) {
    try {
        std::filesystem::path inputPath(inputFile);
        std::filesystem::path inputDir = inputPath.parent_path();
        std::filesystem::path tempDir = inputDir / "temp";

        if (!std::filesystem::exists(tempDir)) {
            std::filesystem::create_directory(tempDir);
            if (verbose) TerminalFormatter::printInfo("Created temp directory: " + tempDir.string());
        }

        if (verbose) {
            TerminalFormatter::printSection("Starting Multi-File Compilation");
            TerminalFormatter::printInfo("Entry file: " + inputFile);
        }

        // Use ImportResolver to find all dependencies
        ImportResolver resolver;

        // Add custom import paths
        for (const auto& path : importPaths) {
            resolver.addSearchPath(path);
            if (verbose) TerminalFormatter::printInfo("Added import path: " + path);
        }

        // Add current directory of entry file
        resolver.addSearchPath(inputDir.string());

        auto all_files = resolver.resolveAllDependencies(inputFile);

        if (verbose) {
            TerminalFormatter::printSuccess("Found " + std::to_string(all_files.size()) + " files to compile:");
            for (const auto& file : all_files) {
                TerminalFormatter::printInfo("  - " + file);
            }
        }

        // Generate .h and .cpp for each file
        const auto stems = computeModuleStems(all_files);
        generateMultiFileOutput(all_files, tempDir.string(), verbose, resolver, stems);

        if (noCompile) {
            TerminalFormatter::printSuccess("Generated " + std::to_string(all_files.size()) + " header/implementation pairs in: " + tempDir.string());
            return true;
        }

        // Compile all generated .cpp files together
        std::vector<std::string> cppFiles;
        for (const auto& file : all_files) {
            std::string moduleName = stems.at(file);
            cppFiles.push_back((tempDir / (moduleName + ".cpp")).string());
        }

        // The executable lives in the temp directory, named after the output base
        const std::string outputName = std::filesystem::path(outputBase).filename().string();
        std::string exeFile = buildOnly
            ? outputBase + CompilerBackend::exeExtension(compiler)    // --build: leave it next to the source
            : (tempDir / (outputName + CompilerBackend::exeExtension(compiler))).string();

        bool compilationSuccess = buildAndRun(compiler, cppFiles, exeFile, tempDir.string(), verbose, linkerFlags, buildOnly); // --- ADDED linkerFlags ---

        if (!compilationSuccess) {
            TerminalFormatter::printNote("generated files were kept in " + tempDir.string());
            return false;
        }

        if (!keepTemp) {
            try {
                // Remove all generated files
                for (const auto& file : all_files) {
                    std::filesystem::path filePath(file);
                    std::string moduleName = stems.at(file);

                    std::filesystem::remove(tempDir / (moduleName + ".cpp"));
                    std::filesystem::remove(tempDir / (moduleName + ".h"));
                }
                if (!buildOnly) std::filesystem::remove(exeFile);
                std::filesystem::remove(tempDir / "pycpp_runtime.h");

                // Clean up .obj files
                cleanupObjectFiles(tempDir, verbose);

                // Try to remove temp directory if empty
                if (std::filesystem::is_empty(tempDir)) {
                    std::filesystem::remove(tempDir);
                    if (verbose) TerminalFormatter::printInfo("Removed empty temp directory: " + tempDir.string());
                }
            }
            catch (const std::exception& e) {
                if (verbose) TerminalFormatter::printWarning(std::string("Cleanup: ") + e.what());
            }
        }
        else {
            TerminalFormatter::printInfo("Temporary files kept in: " + tempDir.string());
        }

        TerminalFormatter::printSuccess("Successfully compiled and executed " + std::to_string(all_files.size()) + " files");
        return true;
    }
    catch (const std::exception& e) {
        reportFailure(e);
        return false;
    }
}

// Single-file compilation (optional with --single-file flag)
bool compileSingleFile(const std::string& inputFile, const std::string& outputBase, bool verbose, bool keepTemp,
    CompilerKind compiler, const std::string& linkerFlags, bool buildOnly = false) {
    try {
        std::filesystem::path inputPath(inputFile);
        std::filesystem::path inputDir = inputPath.parent_path();
        std::filesystem::path tempDir = inputDir / "temp";

        if (!std::filesystem::exists(tempDir)) {
            std::filesystem::create_directory(tempDir);
            if (verbose) TerminalFormatter::printInfo("Created temp directory: " + tempDir.string());
        }

        std::ifstream file(inputFile);
        if (!file.is_open()) {
            TerminalFormatter::printError("Cannot open input file '" + inputFile + "'");
            return false;
        }

        std::string sourceCode((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        if (sourceCode.length() >= 3 &&
            static_cast<unsigned char>(sourceCode[0]) == 0xEF &&
            static_cast<unsigned char>(sourceCode[1]) == 0xBB &&
            static_cast<unsigned char>(sourceCode[2]) == 0xBF) {
            sourceCode = sourceCode.substr(3);
            if (verbose) TerminalFormatter::printInfo("Removed UTF-8 BOM");
        }

        pycpp::setSource(inputFile, sourceCode);
        Lexer lexer(sourceCode);
        auto tokens = lexer.tokenize();

        PyCppParser parser(tokens);
        auto ast = parser.parse();

        SemanticAnalyzer analyzer;
        analyzer.analyze(ast);

        const std::string outputName = std::filesystem::path(outputBase).filename().string();

        IncludeManager include_manager;
        pycpp::registerClasses(ast.get());
        generateHeaderAndImplementation(ast, (tempDir / outputName).string(), include_manager);

        std::string cppFile = (tempDir / (outputName + ".cpp")).string();
        std::string exeFile = buildOnly
            ? outputBase + CompilerBackend::exeExtension(compiler)    // --build: leave it next to the source
            : (tempDir / (outputName + CompilerBackend::exeExtension(compiler))).string();

        bool compilationSuccess = buildAndRun(compiler, { cppFile }, exeFile, tempDir.string(), verbose, linkerFlags, buildOnly); // --- ADDED linkerFlags ---

        if (!compilationSuccess) {
            TerminalFormatter::printNote("generated files were kept in " + tempDir.string());
            return false;
        }

        if (!keepTemp) {
            try {
                std::filesystem::remove(cppFile);
                std::filesystem::remove(tempDir / (outputName + ".h"));
                if (!buildOnly) std::filesystem::remove(exeFile);
                std::filesystem::remove(tempDir / "pycpp_runtime.h");

                // Clean up .obj files
                cleanupObjectFiles(tempDir, verbose);

                if (std::filesystem::is_empty(tempDir)) {
                    std::filesystem::remove(tempDir);
                    if (verbose) TerminalFormatter::printInfo("Removed empty temp directory: " + tempDir.string());
                }
            }
            catch (const std::exception& e) {
                if (verbose) TerminalFormatter::printWarning(std::string("Cleanup: ") + e.what());
            }
        }
        else {
            TerminalFormatter::printInfo("Temporary files kept in: " + tempDir.string());
        }

        TerminalFormatter::printSuccess("Successfully compiled and executed " + inputFile);
        return true;
    }
    catch (const std::exception& e) {
        reportFailure(e);
        return false;
    }
}

bool compileFile(const std::string& inputFile, const std::string& outputBase, bool verbose, bool keepTemp, bool noCompile, bool singleFile, const std::vector<std::string>& importPaths, CompilerKind compiler, const std::string& linkerFlags, bool buildOnly = false) {
    if (noCompile) {
        try {
            std::ifstream file(inputFile);
            if (!file.is_open()) {
                TerminalFormatter::printError("Cannot open input file '" + inputFile + "'");
                return false;
            }

            std::string sourceCode((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();

            if (sourceCode.length() >= 3 &&
                static_cast<unsigned char>(sourceCode[0]) == 0xEF &&
                static_cast<unsigned char>(sourceCode[1]) == 0xBB &&
                static_cast<unsigned char>(sourceCode[2]) == 0xBF) {
                sourceCode = sourceCode.substr(3);
            }

            pycpp::setSource(inputFile, sourceCode);
        Lexer lexer(sourceCode);
            auto tokens = lexer.tokenize();

            PyCppParser parser(tokens);
            auto ast = parser.parse();

            SemanticAnalyzer analyzer;
            analyzer.analyze(ast);

            IncludeManager include_manager;
            pycpp::registerClasses(ast.get());
            generateHeaderAndImplementation(ast, outputBase, include_manager);

            TerminalFormatter::printResult("Generated " + outputBase + ".h and " + outputBase + ".cpp");
            return true;
        }
        catch (const std::exception& e) {
            reportFailure(e);
            return false;
        }
    }
    else if (singleFile) {
        // Use single-file compilation only when explicitly requested
        return compileSingleFile(inputFile, outputBase, verbose, keepTemp, compiler, linkerFlags, buildOnly); // --- ADDED linkerFlags ---
    }
    else {
        // DEFAULT: Use multi-file compilation with auto-dependency resolution
        return compileMultiFile(inputFile, outputBase, importPaths, verbose, keepTemp, noCompile, compiler, linkerFlags, buildOnly); // --- ADDED linkerFlags ---
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string inputFile;
    std::string outputBase;
    bool verbose = false;
    bool keepTemp = false;
    bool noCompile = false;
    bool singleFile = false;  // Default is multi-file compilation
    bool buildOnly = false;   // -b / --build
    CompilerKind requestedCompiler = CompilerKind::Auto;
    std::vector<std::string> importPaths;
    std::string linkerFlags; // --- ADDED ---

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
            pycpp::setVerbose(true);
        }
        else if (arg == "--keep-temp") {
            keepTemp = true;
        }
        else if (arg == "-b" || arg == "--build") {
            buildOnly = true;
        }
        else if (arg == "--no-compile") {
            noCompile = true;
        }
        else if (arg == "--single-file") {
            singleFile = true;
        }
        else if (arg == "--compiler" || arg.rfind("--compiler=", 0) == 0) {
            std::string value;
            if (arg == "--compiler") {
                if (i + 1 < argc) value = argv[++i];
                else {
                    TerminalFormatter::printError("--compiler option requires a value (gcc, msvc or auto)");
                    return 1;
                }
            }
            else {
                value = arg.substr(std::string("--compiler=").size());
            }
            if (!CompilerBackend::parseKind(value, requestedCompiler)) {
                TerminalFormatter::printError("Unknown compiler '" + value + "' (expected gcc, msvc or auto)");
                return 1;
            }
        }
        else if (arg == "--linker-flags") { // --- ADDED ---
            if (i + 1 < argc) linkerFlags = argv[++i];
            else {
                TerminalFormatter::printError("--linker-flags option requires a value");
                return 1;
            }
        }
        else if (arg == "-I") {
            if (i + 1 < argc) {
                importPaths.push_back(argv[++i]);
            }
            else {
                TerminalFormatter::printError("-I option requires a directory path");
                return 1;
            }
        }
        else if (arg == "-o") {
            if (i + 1 < argc) outputBase = argv[++i];
            else {
                TerminalFormatter::printError("-o option requires an output filename");
                return 1;
            }
        }
        else if (arg[0] == '-') {
            TerminalFormatter::printError("Unknown option '" + arg + "'");
            printUsage(argv[0]);
            return 1;
        }
        else inputFile = arg;
    }

    if (inputFile.empty()) {
        TerminalFormatter::printError("No input file specified");
        printUsage(argv[0]);
        return 1;
    }

    if (inputFile.length() < 7 || inputFile.substr(inputFile.length() - 6) != ".pycpp") {
        TerminalFormatter::printWarning("Input file '" + inputFile + "' doesn't have .pycpp extension");
    }

    if (outputBase.empty()) {
        size_t lastDot = inputFile.find_last_of('.');
        outputBase = (lastDot != std::string::npos) ? inputFile.substr(0, lastDot) : inputFile;
    }

    if (verbose) {
        TerminalFormatter::printSection("PyCPP Compiler");
        TerminalFormatter::printInfo("Input file: " + inputFile);
        TerminalFormatter::printInfo("Output base: " + outputBase);
        TerminalFormatter::printInfo("Mode: " + std::string(singleFile ? "Single-file" : "Multi-file with auto-deps"));
        TerminalFormatter::printInfo("Verbose: " + std::string(verbose ? "Yes" : "No"));
        TerminalFormatter::printInfo("Keep temp: " + std::string(keepTemp ? "Yes" : "No"));
        TerminalFormatter::printInfo("No compile: " + std::string(noCompile ? "Yes" : "No"));
        if (!importPaths.empty()) {
            std::string paths;
            for (const auto& path : importPaths) {
                paths += path + " ";
            }
            TerminalFormatter::printInfo("Import paths: " + paths);
        }
        if (!linkerFlags.empty()) { // --- ADDED ---
            TerminalFormatter::printInfo("Linker flags: " + linkerFlags);
        }
    }

    const CompilerKind compiler = CompilerBackend::resolve(requestedCompiler);
    if (verbose) {
        TerminalFormatter::printInfo("Compiler: " + CompilerBackend::displayName(compiler));
    }

    bool success = compileFile(inputFile, outputBase, verbose, keepTemp, noCompile, singleFile, importPaths, compiler, linkerFlags, buildOnly); // --- ADDED linkerFlags ---
    return success ? g_programExitCode : 1;
}