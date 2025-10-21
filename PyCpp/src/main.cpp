#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>
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

// Terminal output formatting
class TerminalFormatter {
public:
    static void printInfo(const std::string& message) {
        std::cout << "   " << message << std::endl;
    }

    static void printSuccess(const std::string& message) {
        std::cout << "   " << message << std::endl;
    }

    static void printWarning(const std::string& message) {
        std::cout << "   " << message << std::endl;
    }

    static void printError(const std::string& message) {
        std::cerr << "   " << message << std::endl;
    }

    static void printFile(const std::string& message) {
        std::cout << "   " << message << std::endl;
    }

    static void printCompile(const std::string& message) {
        std::cout << "   " << message << std::endl;
    }

    static void printRun(const std::string& message) {
        std::cout << "   " << message << std::endl;
    }

    static void printSection(const std::string& message) {
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << " " << message << std::endl;
        std::cout << std::string(60, '=') << std::endl;
    }

    static void printSubsection(const std::string& message) {
        std::cout << "   " << message << std::endl;
    }

    static void printProgramOutput(const std::string& output) {
        std::cout << "   " << output;
    }

    static void printCompilerOutput(const std::string& output) {
        if (!output.empty() && output != "\n") {
            std::cout << "   " << output;
        }
    }

    // New methods for clean formatting
    static void printSettingUp() {
        std::cout << "   Setting up Visual Studio environment..." << std::endl;
    }

    static void printCompiling(const std::string& filename) {
        std::cout << "   Compiling " << filename << "..." << std::endl;
    }

    static void printFilename(const std::string& filename) {
        std::cout << "   " << filename << std::endl;
    }

    static void printRunning(const std::string& filename) {
        std::cout << "   Running " << filename << "..." << std::endl;
    }

    static void printProgramOutputLine(const std::string& line) {
        std::cout << "   " << line << std::endl;
    }

    static void printExitCode(int code) {
        std::cout << "   PROGRAM_EXIT_CODE:" << code << std::endl;
    }
};

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

    // Generate header file with ALL class/struct/template definitions
    HeaderGenerator header_gen;
    std::string header_content = header_gen.generate(ast, base_filename, manual_includes);

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
    TerminalFormatter::printSuccess("Total files generated: 2");
}

// Generate multiple files for multi-file compilation
void generateMultiFileOutput(const std::vector<std::string>& files, const std::string& tempDir, bool verbose) {
    for (const auto& file : files) {
        if (verbose) TerminalFormatter::printInfo("Processing: " + file);

        std::ifstream infile(file);
        if (!infile.is_open()) {
            TerminalFormatter::printError("Cannot open file '" + file + "'");
            continue;
        }

        std::string sourceCode((std::istreambuf_iterator<char>(infile)), std::istreambuf_iterator<char>());
        infile.close();

        // Remove BOM if present
        if (sourceCode.length() >= 3 &&
            static_cast<unsigned char>(sourceCode[0]) == 0xEF &&
            static_cast<unsigned char>(sourceCode[1]) == 0xBB &&
            static_cast<unsigned char>(sourceCode[2]) == 0xBF) {
            sourceCode = sourceCode.substr(3);
        }

        Lexer lexer(sourceCode);
        auto tokens = lexer.tokenize();

        PyCppParser parser(tokens);
        auto ast = parser.parse();

        SemanticAnalyzer analyzer;
        analyzer.analyze(ast);

        // Generate output filename from input filename
        std::filesystem::path filePath(file);
        std::string moduleName = filePath.stem().string(); // Remove extension

        IncludeManager include_manager;
        generateHeaderAndImplementation(ast, (std::filesystem::path(tempDir) / moduleName).string(), include_manager);
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
    std::cout << "  --no-compile     Only generate .h and .cpp files, don't compile\n";
    std::cout << "  --single-file    Compile only the specified file (no auto-deps)\n";
    std::cout << "  -I <path>        Add directory to import search path\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << programName << " program.pycpp                          # Multi-file compilation with auto-deps\n";
    std::cout << "  " << programName << " program.pycpp -o myprogram            # Custom output name\n";
    std::cout << "  " << programName << " program.pycpp -v                      # Verbose output\n";
    std::cout << "  " << programName << " program.pycpp --keep-temp             # Keep temporary files\n";
    std::cout << "  " << programName << " program.pycpp --no-compile            # Generate C++ only\n";
    std::cout << "  " << programName << " program.pycpp --single-file           # Compile only one file\n";
    std::cout << "  " << programName << " program.pycpp -I ./lib               # With custom import path\n";
}

bool runCompilerWithVSBatch(const std::string& cppFile, const std::string& exeFile, const std::string& includeDir, bool verbose) {
    std::string batchContent =
        "@echo off\n"
        "echo Setting up Visual Studio environment...\n"
        "call \"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat\" >nul 2>&1\n"
        "if %errorlevel% neq 0 (\n"
        "  echo Failed to set up Visual Studio environment\n"
        "  exit /b 1\n"
        ")\n"
        "echo Compiling " + cppFile + "...\n"
        "cl /EHsc /std:c++17 \"" + cppFile + "\" /Fe\"" + exeFile + "\" /I\"" + includeDir + "\" /nologo\n"
        "if %errorlevel% neq 0 exit /b 1\n"
        "for /f \"tokens=*\" %%i in ('dir /b \"*.cpp\" 2^>nul') do echo    %%i\n"
        "echo.\n"
        "echo Running " + exeFile + "...\n"
        "echo.\n"
        "\"" + exeFile + "\"\n"
        "set EXIT_CODE=%errorlevel%\n"
        "echo.\n"
        "echo PROGRAM_EXIT_CODE:%EXIT_CODE%\n";

    std::string batchFile = includeDir + "\\compile_vs.bat";
    std::ofstream batch(batchFile);
    if (!batch) {
        TerminalFormatter::printError("Could not create batch file");
        return false;
    }
    batch << batchContent;
    batch.close();

    if (verbose) {
        TerminalFormatter::printInfo("Created batch file: " + batchFile);
    }

    std::string command = "cmd /c \"" + batchFile + "\"";
    if (verbose) {
        TerminalFormatter::printInfo("Executing: cmd /c " + batchFile);
    }

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        TerminalFormatter::printError("Could not start compiler process");
        return false;
    }

    char buffer[256];
    bool compilationSuccess = false;
    bool programRan = false;

    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        std::string line(buffer);
        if (!line.empty() && line[line.length() - 1] == '\n') {
            line.erase(line.length() - 1);
        }

        // Filter and format output exactly as requested
        if (line.find("Setting up") != std::string::npos) {
            TerminalFormatter::printSettingUp();
        }
        else if (line.find("Compiling") != std::string::npos) {
            TerminalFormatter::printCompiling(cppFile);
        }
        else if (line.find(".cpp") != std::string::npos && line.length() < 50) {
            // This is the filename output from compiler
            TerminalFormatter::printFilename(line);
        }
        else if (line.find("Running") != std::string::npos) {
            TerminalFormatter::printRunning(exeFile);
            std::cout << std::endl; // Add the blank line before program output
        }
        else if (line.find("PROGRAM_EXIT_CODE:") != std::string::npos) {
            programRan = true;
            compilationSuccess = true;
            TerminalFormatter::printExitCode(std::stoi(line.substr(18)));
        }
        else if (!line.empty() && line != "." && line.find("Setting up") == std::string::npos &&
            line.find("Compiling") == std::string::npos && line.find("Running") == std::string::npos) {
            // This is actual program output - indent it
            TerminalFormatter::printProgramOutputLine(line);
        }
    }

    int result = pclose(pipe);

    // Clean up batch file and stray object files
    std::remove(batchFile.c_str());
    cleanupStrayObjectFiles(verbose);

    return compilationSuccess && programRan;
}

// Multi-file compilation with automatic dependency resolution (DEFAULT)
bool compileMultiFile(const std::string& inputFile, const std::string& outputBase,
    const std::vector<std::string>& importPaths, bool verbose, bool keepTemp, bool noCompile) {
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
        generateMultiFileOutput(all_files, tempDir.string(), verbose);

        if (noCompile) {
            TerminalFormatter::printSuccess("Generated " + std::to_string(all_files.size()) + " header/implementation pairs in: " + tempDir.string());
            return true;
        }

        // Compile all generated .cpp files together
        std::string cppFiles;
        for (const auto& file : all_files) {
            std::filesystem::path filePath(file);
            std::string moduleName = filePath.stem().string();
            cppFiles += "\"" + (tempDir / (moduleName + ".cpp")).string() + "\" ";
        }

        std::string exeFile = (tempDir / (outputBase + ".exe")).string();
        std::string compileCommand = "cl /EHsc /std:c++17 " + cppFiles + " /Fe\"" + exeFile + "\" /I\"" + tempDir.string() + "\" /nologo";

        if (verbose) {
            TerminalFormatter::printCompile("Compiling with command: " + compileCommand);
        }

        bool compilationSuccess = runCompilerWithVSBatch((tempDir / (outputBase + ".cpp")).string(), exeFile, tempDir.string(), verbose);

        if (!compilationSuccess) {
            TerminalFormatter::printCompile("Compiling generated C++ code...");
            int compileResult = system(compileCommand.c_str());
            compilationSuccess = (compileResult == 0 && std::filesystem::exists(exeFile));

            if (compilationSuccess) {
                TerminalFormatter::printSuccess("Executable created: " + exeFile);
                TerminalFormatter::printRun("Running program...");
                int runResult = system(("\"" + exeFile + "\"").c_str());
                if (verbose) TerminalFormatter::printInfo("Program exited with code: " + std::to_string(runResult));
            }
        }

        if (!compilationSuccess) {
            TerminalFormatter::printWarning("Could not automatically compile the generated C++ code");
            TerminalFormatter::printInfo("Generated files are in: " + tempDir.string());
            return false;
        }

        if (!keepTemp) {
            try {
                // Remove all generated files
                for (const auto& file : all_files) {
                    std::filesystem::path filePath(file);
                    std::string moduleName = filePath.stem().string();

                    std::filesystem::remove(tempDir / (moduleName + ".cpp"));
                    std::filesystem::remove(tempDir / (moduleName + ".h"));
                }
                std::filesystem::remove(exeFile);

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
        TerminalFormatter::printError("Multi-file compilation failed: " + std::string(e.what()));
        return false;
    }
}

// Single-file compilation (optional with --single-file flag)
bool compileSingleFile(const std::string& inputFile, const std::string& outputBase, bool verbose, bool keepTemp) {
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

        Lexer lexer(sourceCode);
        auto tokens = lexer.tokenize();

        PyCppParser parser(tokens);
        auto ast = parser.parse();

        SemanticAnalyzer analyzer;
        analyzer.analyze(ast);

        IncludeManager include_manager;
        generateHeaderAndImplementation(ast, (tempDir / outputBase).string(), include_manager);

        std::string cppFile = (tempDir / (outputBase + ".cpp")).string();
        std::string exeFile = (tempDir / (outputBase + ".exe")).string();

        bool compilationSuccess = runCompilerWithVSBatch(cppFile, exeFile, tempDir.string(), verbose);

        if (!compilationSuccess) {
            TerminalFormatter::printCompile("Compiling generated C++ code...");
            std::string compileCommand = "cl /EHsc /std:c++17 \"" + cppFile + "\" /Fe\"" + exeFile + "\" /I\"" + tempDir.string() + "\" /nologo";
            int compileResult = system(compileCommand.c_str());

            if (compileResult == 0 && std::filesystem::exists(exeFile)) {
                TerminalFormatter::printSuccess("Executable created: " + exeFile);
                TerminalFormatter::printRun("Running program...");
                int runResult = system(("\"" + exeFile + "\"").c_str());
                if (verbose) TerminalFormatter::printInfo("Program exited with code: " + std::to_string(runResult));
                compilationSuccess = true;
            }
        }

        if (!compilationSuccess) {
            TerminalFormatter::printWarning("Could not automatically compile the generated C++ code");
            TerminalFormatter::printInfo("Generated files are in: " + tempDir.string());
            return false;
        }

        if (!keepTemp) {
            try {
                std::filesystem::remove(cppFile);
                std::filesystem::remove(tempDir / (outputBase + ".h"));
                std::filesystem::remove(exeFile);

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
        TerminalFormatter::printError("Compilation failed: " + std::string(e.what()));
        return false;
    }
}

bool compileFile(const std::string& inputFile, const std::string& outputBase, bool verbose, bool keepTemp, bool noCompile, bool singleFile, const std::vector<std::string>& importPaths) {
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

            Lexer lexer(sourceCode);
            auto tokens = lexer.tokenize();

            PyCppParser parser(tokens);
            auto ast = parser.parse();

            SemanticAnalyzer analyzer;
            analyzer.analyze(ast);

            IncludeManager include_manager;
            generateHeaderAndImplementation(ast, outputBase, include_manager);

            TerminalFormatter::printSuccess("Generated " + outputBase + ".h and " + outputBase + ".cpp");
            return true;
        }
        catch (const std::exception& e) {
            TerminalFormatter::printError("Compilation failed: " + std::string(e.what()));
            return false;
        }
    }
    else if (singleFile) {
        // Use single-file compilation only when explicitly requested
        return compileSingleFile(inputFile, outputBase, verbose, keepTemp);
    }
    else {
        // DEFAULT: Use multi-file compilation with auto-dependency resolution
        return compileMultiFile(inputFile, outputBase, importPaths, verbose, keepTemp, noCompile);
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
    std::vector<std::string> importPaths;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        }
        else if (arg == "--keep-temp") {
            keepTemp = true;
        }
        else if (arg == "--no-compile") {
            noCompile = true;
        }
        else if (arg == "--single-file") {
            singleFile = true;
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
    }

    bool success = compileFile(inputFile, outputBase, verbose, keepTemp, noCompile, singleFile, importPaths);
    return success ? 0 : 1;
}