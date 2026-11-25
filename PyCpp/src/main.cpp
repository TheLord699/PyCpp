#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../include/lexer.h"
#include "../include/pycpp_parser.h"
#include "../include/semantic_analyzer.h"
#include "../include/include_manager.h"
#include "../include/code_generator.h"
#include "../include/import_resolver.h"
#include "../include/compiler_backend.h"
#include "../include/log.h"
#include "../include/pycpp_runtime_text.h"
#include "../include/diagnostics.h"

namespace fs = std::filesystem;

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
    static void printSubsection(const std::string& message) { status(message); }
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

    // [diag-fix] every message goes through the diagnostic queue
    static void emitPlain(pycpp::Severity severity, const std::string& code, const std::string& message,
                          const std::string& hint) {
        pycpp::Diagnostic d;
        d.severity = severity;
        d.code = code;
        d.message = message;
        d.hint = hint;
        pycpp::emit(d);
    }

    static void printNote(const std::string& message, const std::string& hint = "") {
        emitPlain(pycpp::Severity::Note, "", message, hint);
    }

    static void printWarning(const std::string& code, const std::string& message, const std::string& hint = "") {
        emitPlain(pycpp::Severity::Warning, code, message, hint);
    }

    static void printError(const std::string& code, const std::string& message, const std::string& hint = "") {
        emitPlain(pycpp::Severity::Error, code, message, hint);
    }

    // Compiler diagnostics (g++/cl output) are always shown.
    static void printCompilerOutput(const std::string& output) {
        if (!output.empty() && output != "\n") {
            std::cerr << output;
        }
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
    // [diag-fix]
    if (auto ce = dynamic_cast<const pycpp::CompileError*>(&e)) {
        size_t errors = 0;
        for (const auto& d : ce->diagnostics) {
            pycpp::emit(d);
            if (d.severity == pycpp::Severity::Error) ++errors;
        }
        pycpp::printFailureSummary(errors == 0 ? 1 : errors);
    }
    else {
        pycpp::Diagnostic d;
        d.message = e.what();
        if (d.message.rfind("cannot open", 0) == 0) {
            d.code = "E0302";
            d.hint = "check the path, and the directory you are running pycpp from";
        }
        pycpp::emit(d);
        pycpp::printFailureSummary(1);
    }
}

// ---- options ----

struct Options {
    std::string inputFile;
    std::string outputBase;
    bool verbose = false;
    bool keepTemp = false;
    bool noCompile = false;
    bool noLineDirectives = false;
    bool singleFile = false;
    bool buildOnly = false;
    bool checkOnly = false;   // `pycpp check file` / --check: stop after analysis
    bool watch = false;       // --watch: run again whenever a source file changes
    bool useCache = true;     // --no-cache: rebuild every module
    unsigned jobs = 0;        // -j N (0: one per CPU core)
    CompilerKind compiler = CompilerKind::Auto;
    std::vector<std::string> importPaths;
    std::string linkerFlags;
};

// Write `content` unless the file already holds exactly that (keeps timestamps stable).
static void writeIfChanged(const std::string& path, const std::string& content) {
    {
        std::ifstream in(path, std::ios::binary);
        if (in) {
            const std::string existing((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (existing == content) return;
        }
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot open '" + path + "' for writing");
    out << content;
}

void generateHeaderAndImplementation(std::unique_ptr<Program>& ast,
    const std::string& base_filename,
    IncludeManager& include_manager) {
    TerminalFormatter::printSubsection("Generating Header and Implementation Files");

    // Generate the header: class/struct/template/constexpr definitions plus
    // prototypes of every function the source file defines.
    std::string guard_name = fs::path(base_filename).filename().string();
    for (char& c : guard_name) c = std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : '_';
    guard_name = "PYCPP_" + guard_name + "_H";

    // Every module's declarations live in their own namespace, named after its generated
    // file; importing modules use the same name (ImportStmt::ns).
    std::string module_ns = fs::path(base_filename).filename().string();
    for (char& c : module_ns) if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
    module_ns = "pycpp_" + module_ns;

    CodeGenerator header_gen;
    header_gen.setNamespace(module_ns);
    const std::string header_content = header_gen.generateHeader(ast, include_manager, guard_name);
    writeIfChanged(base_filename + ".h", header_content);
    TerminalFormatter::printFile("Generated header: " + base_filename + ".h");

    // Implementation file with only function implementations
    CodeGenerator code_gen;
    code_gen.setNamespace(module_ns);
    include_manager.trackManualInclude("\"" + base_filename + ".h\"");
    const std::string cpp_content = code_gen.generate(ast, include_manager);
    writeIfChanged(base_filename + ".cpp", cpp_content);
    TerminalFormatter::printFile("Generated implementation: " + base_filename + ".cpp");

    if (include_manager.isHeaderIncluded("\"pycpp_runtime.h\"")) {
        const fs::path dir = fs::path(base_filename).parent_path();
        writeIfChanged(((dir.empty() ? fs::path(".") : dir) / "pycpp_runtime.h").string(), kPycppRuntime);
    }
}

static std::string readSource(const std::string& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open '" + file + "'");
    std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (src.size() >= 3 && (unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF)
        src.erase(0, 3);
    return src;
}

// Read, lex and parse one module.
static std::unique_ptr<Program> parseModule(const std::string& file) {
    const std::string src = readSource(file);
    pycpp::setSource(file, src);            // for the code frames in diagnostics
    Lexer lexer(src);
    PyCppParser parser(lexer.tokenize());
    return parser.parse();
}

// Name (without extension) of the generated .h/.cpp pair of every module.
// It is the module's path relative to the entry file's folder, with separators turned
// into '_':   graphics/shapes.pycpp   -> graphics_shapes
//             graphics/__init__.pycpp -> graphics        (a package is named after its folder)
// so modules with the same file name in different folders never share a generated name.
static std::map<std::string, std::string> computeModuleStems(const std::vector<std::string>& files) {
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
        if (!inserted.second && inserted.first->second != file) {
            pycpp::Diagnostic d;
            d.code = "E0203";
            d.message = "modules '" + inserted.first->second + "' and '" + file +
                        "' would both generate '" + stem + ".h'";
            d.hint = "rename one of them";
            throw pycpp::CompileError(d);
        }
        stems[file] = stem;
    }
    return stems;
}

// ---- front end: every module parsed, checked and ready for code generation ----

struct FrontEnd {
    std::vector<std::pair<std::string, std::unique_ptr<Program>>> modules;   // entry first
    std::map<std::string, std::string> stems;                               // file -> generated name
};

// Parse every module first, then analyse them in dependency order (a module after the
// modules it imports), so calls into another module are checked against its signatures.
static FrontEnd runFrontEnd(const Options& opt, bool requireMain) {
    FrontEnd fe;
    ImportResolver resolver;
    for (const auto& path : opt.importPaths) resolver.addSearchPath(path);
    const fs::path inputDir = fs::path(opt.inputFile).parent_path();
    resolver.addSearchPath(inputDir.string());

    std::vector<std::string> files;
    if (opt.singleFile || opt.noCompile) {
        files.push_back(opt.inputFile);
    }
    else {
        files = resolver.resolveAllDependencies(opt.inputFile);
        if (opt.verbose) {
            TerminalFormatter::printSuccess("Found " + std::to_string(files.size()) + " files to compile:");
            for (const auto& file : files) TerminalFormatter::printInfo("  - " + file);
        }
    }
    fe.stems = computeModuleStems(files);
    if (opt.singleFile || opt.noCompile) {
        // a single module is named after the output
        fe.stems[files.front()] = fs::path(opt.outputBase).filename().string();
    }

    for (const auto& file : files) {
        if (opt.verbose) TerminalFormatter::printInfo("Parsing: " + file);
        fe.modules.emplace_back(file, parseModule(file));
    }

    // [fix2] the entry file must define main()
    if (requireMain && !fe.modules.empty()) {
        bool has_main = false;
        for (auto& s : fe.modules.front().second->statements)
            if (auto f = dynamic_cast<FunctionDef*>(s.get()))
                if (f->name == "main") has_main = true;
        if (!has_main) {
            pycpp::Diagnostic d;
            d.code = "E0202";
            d.message = "the entry file has no 'main' function";
            d.file = fe.modules.front().first;
            d.hint = "add `def main() -> int:` to the file you pass to pycpp";
            d.notes.push_back("without it the C++ linker fails with 'undefined reference to main'");
            throw pycpp::CompileError(d);
        }
    }

    // Point every import at the header (and namespace) its target file generates, and
    // find the imports that are part of an import cycle
    std::map<std::string, std::vector<std::string>> graph;   // file -> files it imports
    if (!opt.singleFile && !opt.noCompile) {
        for (auto& [file, ast] : fe.modules) {
            for (auto& stmt : ast->statements) {
                auto import_stmt = dynamic_cast<ImportStmt*>(stmt.get());
                if (!import_stmt) continue;
                const std::string target = resolver.findModuleFile(import_stmt->module, file);
                auto it = fe.stems.find(target);
                if (it != fe.stems.end()) import_stmt->header_stem = it->second;
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
        for (auto& [file, ast] : fe.modules) {
            for (auto& stmt : ast->statements) {
                auto import_stmt = dynamic_cast<ImportStmt*>(stmt.get());
                if (!import_stmt) continue;
                const std::string target = resolver.findModuleFile(import_stmt->module, file);
                import_stmt->cyclic = !target.empty() && reaches(target, file);
            }
        }
    }

    // Dependencies before the modules that import them (post-order of the import graph)
    std::vector<size_t> order;
    {
        std::map<std::string, size_t> index;
        for (size_t i = 0; i < fe.modules.size(); ++i) index[fe.modules[i].first] = i;
        std::set<std::string> visited;
        std::function<void(const std::string&)> visit = [&](const std::string& file) {
            if (!visited.insert(file).second) return;
            for (const auto& dep : graph[file]) visit(dep);
            auto it = index.find(file);
            if (it != index.end()) order.push_back(it->second);
        };
        for (auto& module : fe.modules) visit(module.first);
    }

    SemanticAnalyzer::resetModuleExports();
    for (size_t i : order) {
        auto& [file, ast] = fe.modules[i];
        if (opt.verbose) TerminalFormatter::printInfo("Analysing: " + file);
        pycpp::setSource(file, readSource(file));
        SemanticAnalyzer analyzer;
        analyzer.setModuleStem(fe.stems.at(file));
        analyzer.analyze(ast);
    }
    for (auto& module : fe.modules) pycpp::registerClasses(module.second.get());
    return fe;
}

// Turn raw C++ compiler output into one short PyCPP diagnostic. The C++ output itself
// is only shown with --verbose.
static void explainBuildFailure(const std::vector<std::string>& log, const std::string& tempDir) {
    // [diag-fix] turn the raw C++ compiler output into one short PyCPP diagnostic
    pycpp::Diagnostic d;
    std::string first_error;
    auto shorten = [](std::string line) {
        size_t cut = line.find(".cpp:");
        const size_t h = line.find(".h:");
        if (h < cut) cut = h;
        if (cut != std::string::npos) {
            const size_t slash = line.find_last_of("/\\", cut);
            if (slash != std::string::npos) line.erase(0, slash + 1);
        }
        return line;
    };
    for (const auto& line : log) {
        size_t p;
        if (first_error.empty() && (line.find(": error:") != std::string::npos ||
            line.find(": fatal error:") != std::string::npos || line.find(" error C") != std::string::npos))
            first_error = shorten(line);

        if ((p = line.find("undefined reference to `")) != std::string::npos) {
            size_t s = p + 24, e = line.find('\'', s);
            d.message = "linking failed: cannot find '" + line.substr(s, e == std::string::npos ? std::string::npos : e - s) + "'";
        }
        else if ((p = line.find("unresolved external symbol ")) != std::string::npos) {
            size_t s = p + 27, e = line.find(" referenced", s);
            d.message = "linking failed: cannot find " + line.substr(s, e == std::string::npos ? std::string::npos : e - s);
        }
        if (!d.message.empty()) {
            d.code = "E0205";
            d.hint = "the library that defines it isn't linked, pass it with --linker-flags \"-l<name>\"";
            break;
        }
        if ((p = line.find("fatal error: ")) != std::string::npos && line.find("No such file") != std::string::npos) {
            size_t s = p + 13;
            d.code = "E0205";
            d.message = "cannot find C++ header '" + line.substr(s, line.find(':', s) - s) + "'";
            d.hint = "check the name in your `use <...>` line and that the library is installed";
            break;
        }
        if ((p = line.find("C1083")) != std::string::npos) {
            d.code = "E0205";
            d.message = "cannot find a C++ header: " + line.substr(p + 7);
            d.hint = "check your `use <...>` lines and that the library is installed";
            break;
        }
    }
    if (d.message.empty()) {   // [fix2] #line directives make g++/cl report positions in the .pycpp file
        for (const auto& line : log) {
            if (line.find("error") == std::string::npos) continue;
            std::string path, msg;
            int ln = 0;
            size_t p = line.find(".pycpp:");
            if (p != std::string::npos) {
                path = line.substr(0, p + 6);
                try { ln = std::stoi(line.substr(p + 7)); } catch (...) { continue; }
                const size_t e = line.find("error:", p);
                if (e == std::string::npos) continue;
                msg = line.substr(e + 6);
            }
            else if ((p = line.find(".pycpp(")) != std::string::npos) {
                path = line.substr(0, p + 6);
                try { ln = std::stoi(line.substr(p + 7)); } catch (...) { continue; }
                const size_t e = line.find(": error ", p);
                if (e == std::string::npos) continue;
                msg = line.substr(e + 8);
            }
            else continue;
            while (!msg.empty() && msg.front() == ' ') msg.erase(0, 1);
            std::ifstream in(path, std::ios::binary);
            if (!in) continue;
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            pycpp::setSource(path, text);
            const auto& sl = pycpp::source().lines;
            if (ln < 1 || ln > static_cast<int>(sl.size())) continue;
            const std::string& src_line = sl[ln - 1];
            const size_t first = src_line.find_first_not_of(" \t");
            if (first == std::string::npos) continue;
            d.code = "E0204";
            d.message = "the C++ compiler rejected this line: " + msg;
            d.line = ln;
            d.column = static_cast<int>(first) + 1;
            d.length = static_cast<int>(src_line.find_last_not_of(" \t") - first + 1);
            d.hint = "PyCPP could not detect this itself. If the PyCPP code is correct this is a PyCPP bug; "
                     "re-run with --verbose for the full C++ output";
            break;
        }
    }
    if (d.message.empty()) {
        d.code = "E0204";
        d.message = "the generated C++ failed to compile";
        d.hint = "this is a PyCPP bug or an unsupported construct. Re-run with --verbose to see the "
                 "C++ compiler output, or --keep-temp to inspect " + tempDir;
        if (!first_error.empty()) d.notes.push_back("the C++ compiler reported:\n" + first_error);
    }
    pycpp::emit(d);
}


// Exit code of the program that was built and run; becomes pycpp's own exit code.
static int g_programExitCode = 0;

// ---- object cache ----

// 64-bit FNV-1a
static unsigned long long fnv1a(const std::string& data, unsigned long long h = 1469598103934665603ULL) {
    for (unsigned char c : data) { h ^= c; h *= 1099511628211ULL; }
    return h;
}

// Hash of everything that determines a module's object file: its generated .cpp, every
// generated header it includes (directly or not) and the compile command.
static std::string unitHash(const std::string& cppFile, const std::string& includeDir, const std::string& flags) {
    unsigned long long h = fnv1a(flags);
    std::vector<std::string> todo{ cppFile };
    std::set<std::string> seen;
    while (!todo.empty()) {
        const std::string file = todo.back();
        todo.pop_back();
        if (!seen.insert(file).second) continue;
        std::ifstream in(file, std::ios::binary);
        if (!in) continue;
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        h = fnv1a(file.substr(file.find_last_of("/\\") + 1) + "\n" + text, h);
        std::istringstream lines(text);
        for (std::string line; std::getline(lines, line);) {
            const size_t inc = line.find("#include \"");
            if (inc == std::string::npos) continue;
            const size_t start = inc + 10, end = line.find('"', start);
            if (end == std::string::npos) continue;
            todo.push_back((fs::path(includeDir) / line.substr(start, end - start)).string());
        }
    }
    std::ostringstream hex;
    hex << std::hex << std::setw(16) << std::setfill('0') << h;
    return hex.str();
}

static std::string readSmallFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

// Compile (only what changed), link and run. Returns true when the program was built
// (and, unless --build, started).
static bool buildAndRun(const Options& opt, CompilerKind compiler, const std::vector<std::string>& cppFiles,
                        const std::string& exeFile, const std::string& tempDir) {
    std::string reason;
    if (!CompilerBackend::isAvailable(compiler, reason)) {
        const size_t cut = reason.find(". ");
        if (cut == std::string::npos) TerminalFormatter::printError("E0303", reason);
        else TerminalFormatter::printError("E0303", reason.substr(0, cut), reason.substr(cut + 2));
        return false;
    }

    const fs::path cacheDir = fs::path(tempDir) / ".cache";
    if (opt.useCache) fs::create_directories(cacheDir);
    const std::string flags = CompilerBackend::buildObjectCommand(compiler, "<in>", "<out>", "<dir>", opt.buildOnly);

    BuildRequest request;
    request.exeFile = exeFile;
    request.includeDir = tempDir;
    request.linkerFlags = opt.linkerFlags;
    request.optimize = opt.buildOnly;
    request.jobs = opt.jobs ? opt.jobs : std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::string> hashes;
    size_t reused = 0;
    for (const auto& cpp : cppFiles) {
        const std::string stem = fs::path(cpp).stem().string();
        BuildUnit unit;
        unit.cppFile = cpp;
        const fs::path dir = opt.useCache ? cacheDir : fs::path(tempDir);
        unit.objFile = (dir / (stem + CompilerBackend::objectExtension(compiler))).string();
        const std::string hash = opt.useCache ? unitHash(cpp, tempDir, flags) : "";
        unit.compile = !opt.useCache || !fs::exists(unit.objFile) ||
                       readSmallFile(dir / (stem + ".hash")) != hash;
        if (!unit.compile) ++reused;
        hashes.push_back(hash);
        request.units.push_back(unit);
    }

    TerminalFormatter::printCompile("Compiling with " + CompilerBackend::displayName(compiler) + " (" +
        std::to_string(cppFiles.size() - reused) + " of " + std::to_string(cppFiles.size()) +
        " modules changed, " + std::to_string(request.jobs) + " jobs)...");
    for (const auto& unit : request.units)
        TerminalFormatter::printFilename(fs::path(unit.cppFile).filename().string() +
                                         (unit.compile ? "" : "  (up to date)"));

    std::vector<std::string> log;
    std::vector<bool> compiled;
    const bool built = CompilerBackend::build(compiler, request, opt.verbose, [&](const std::string& line) {
        log.push_back(line);
        if (opt.verbose && !line.empty()) TerminalFormatter::printCompilerOutput(line + "\n");
    }, compiled);

    // Remember which objects are now current
    if (opt.useCache) {
        for (size_t i = 0; i < request.units.size(); ++i) {
            if (!compiled[i]) continue;
            std::ofstream(cacheDir / (fs::path(request.units[i].cppFile).stem().string() + ".hash")) << hashes[i] << "\n";
        }
    }
    // [fix2] MSVC drops stray .obj files in the current directory
    for (const auto& file : cppFiles) {
        std::error_code ec;
        fs::remove(fs::path(file).stem().string() + ".obj", ec);
    }

    if (!built) {
        if (!opt.verbose) explainBuildFailure(log, tempDir);   // never dump raw C++ errors
        else TerminalFormatter::printError("E0204", "compilation failed");
        return false;
    }

    if (opt.buildOnly) {
        TerminalFormatter::printResult("Built " + exeFile);
        return true;
    }

    TerminalFormatter::printRunning(exeFile);
    pycpp::flushDiagnostics();
    std::cout.flush();
    const int exitCode = CompilerBackend::run(exeFile, [](const std::string& line) { std::cout << line << std::endl; });
    if (exitCode < 0) {
        TerminalFormatter::printError("", "could not start the compiled program");
        return false;
    }
    TerminalFormatter::printExitCode(exitCode);
    g_programExitCode = exitCode;
    return true;
}

// One complete run: check, generate, build and run, as the options ask.
static bool runOnce(const Options& opt, CompilerKind compiler) {
    g_programExitCode = 0;
    try {
        const fs::path inputDir = fs::path(opt.inputFile).parent_path();
        if (opt.verbose) {
            TerminalFormatter::printSection(opt.checkOnly ? "Checking" : "Compiling");
            TerminalFormatter::printInfo("Entry file: " + opt.inputFile);
        }

        FrontEnd fe = runFrontEnd(opt, !opt.checkOnly && !opt.noCompile);

        if (opt.checkOnly) {
            pycpp::flushDiagnostics();
            if (pycpp::errorFormat() == pycpp::ErrorFormat::Human) {
                const int w = pycpp::warningCount();
                std::cerr << "checked " << fe.modules.size() << (fe.modules.size() == 1 ? " file" : " files")
                          << ": no errors" << (w ? ", " + std::to_string(w) + (w == 1 ? " warning" : " warnings") : "")
                          << "\n";
            }
            return true;
        }

        if (opt.noCompile) {
            // --no-compile: <output>.h and <output>.cpp next to the source, nothing else
            IncludeManager include_manager;
            generateHeaderAndImplementation(fe.modules.front().second, opt.outputBase, include_manager);
            pycpp::flushDiagnostics();
            TerminalFormatter::printResult("Generated " + opt.outputBase + ".h and " + opt.outputBase + ".cpp");
            return true;
        }

        const fs::path tempDir = inputDir / "temp";
        fs::create_directories(tempDir);

        std::vector<std::string> cppFiles;
        for (auto& [file, ast] : fe.modules) {
            pycpp::source().name = file;   // [fix2] #line directives name this module
            IncludeManager include_manager;
            const std::string base = (tempDir / fe.stems.at(file)).string();
            generateHeaderAndImplementation(ast, base, include_manager);
            cppFiles.push_back(base + ".cpp");
        }
        pycpp::flushDiagnostics();   // warnings before the program's own output

        const std::string outputName = fs::path(opt.outputBase).filename().string();
        const std::string exeFile = opt.buildOnly
            ? opt.outputBase + CompilerBackend::exeExtension(compiler)    // --build: leave it next to the source
            : (tempDir / (outputName + CompilerBackend::exeExtension(compiler))).string();

        if (!buildAndRun(opt, compiler, cppFiles, exeFile, tempDir.string())) {
            TerminalFormatter::printNote("generated files were kept in " + tempDir.string());
            return false;
        }

        if (!opt.keepTemp) {
            std::error_code ec;
            for (const auto& cpp : cppFiles) {
                fs::remove(cpp, ec);
                fs::remove(fs::path(cpp).replace_extension(".h"), ec);
                fs::remove(fs::path(cpp).replace_extension(".o"), ec);
                fs::remove(fs::path(cpp).replace_extension(".obj"), ec);
            }
            if (!opt.buildOnly) fs::remove(exeFile, ec);
            fs::remove(tempDir / "pycpp_runtime.h", ec);
            // temp/.cache (the object cache) stays, so the next run only recompiles what changed
            if (fs::is_empty(tempDir, ec)) fs::remove(tempDir, ec);
        }
        else {
            TerminalFormatter::printInfo("Temporary files kept in: " + tempDir.string());
        }
        return true;
    }
    catch (const std::exception& e) {
        reportFailure(e);
        return false;
    }
}

// Modification times of every source file of the last run (and the entry file).
static std::map<std::string, fs::file_time_type> sourceTimes(const std::string& entry) {
    std::map<std::string, fs::file_time_type> times;
    std::vector<std::string> files = pycpp::sourceOrder();
    files.push_back(entry);
    for (const auto& f : files) {
        std::error_code ec;
        const auto t = fs::last_write_time(f, ec);
        if (!ec) times[fs::weakly_canonical(f, ec).string()] = t;
    }
    // new .pycpp files next to the entry may be imported after an edit
    std::error_code ec;
    for (const auto& entry_it : fs::directory_iterator(fs::path(entry).parent_path().empty() ? fs::path(".") : fs::path(entry).parent_path(), ec)) {
        if (entry_it.path().extension() == ".pycpp") {
            std::error_code e2;
            times[fs::weakly_canonical(entry_it.path(), e2).string()] = fs::last_write_time(entry_it.path(), e2);
        }
    }
    return times;
}

// Forget everything a previous run learned (--watch).
static void resetState() {
    pycpp::resetDiagnostics();
    pycpp::resetClassRegistry();
    SemanticAnalyzer::resetModuleExports();
}

void printUsage(const std::string& programName) {
    std::cout << "PyCPP Compiler - Compile PyCPP code to C++\n\n";
    std::cout << "Usage: " << programName << " <input_file.pycpp> [options]\n";
    std::cout << "       " << programName << " check <input_file.pycpp> [options]   (analyse only, no C++ build)\n";
    std::cout << "       " << programName << " --explain <code>                     (describe a diagnostic, e.g. E0101)\n\n";
    std::cout << "Options:\n";
    std::cout << "  -o <output>      Specify output base name (without extension)\n";
    std::cout << "  -h, --help       Show this help message\n";
    std::cout << "  -v, --verbose    Enable verbose output\n";
    std::cout << "  --keep-temp      Keep temporary compilation files\n";
    std::cout << "  -b, --build      Build an optimized executable next to the source, don't run it\n";
    std::cout << "  --no-compile     Only generate .h and .cpp files, don't compile\n";
    std::cout << "  --no-line-directives  Omit #line directives from generated C++\n";
    std::cout << "  --single-file    Compile only the specified file (no auto-deps)\n";
    std::cout << "  --check          Stop after parsing and analysis (same as `pycpp check`)\n";
    std::cout << "  --watch          Run again every time a source file changes (Ctrl+C to stop)\n";
    std::cout << "  --error-format <f>  Diagnostics as human (default), short (file:line:col: ...) or json\n";
    std::cout << "  -j, --jobs <n>   Compile up to n modules at once (default: one per CPU core)\n";
    std::cout << "  --no-cache       Recompile every module (by default unchanged modules are reused\n";
    std::cout << "                   from temp/.cache)\n";
    std::cout << "  --compiler <c>   C++ compiler for the generated code: gcc, clang, msvc or auto\n";
    std::cout << "                   (default: auto = msvc on Windows, clang on macOS, gcc elsewhere)\n";
    std::cout << "  --linker-flags <flags>  Extra flags passed to the linker (e.g. \"-lraylib -lopengl32\")\n";
    std::cout << "  -I <path>        Add directory to import search path\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << programName << " program.pycpp                          # Multi-file compilation with auto-deps\n";
    std::cout << "  " << programName << " program.pycpp -o myprogram            # Custom output name\n";
    std::cout << "  " << programName << " check program.pycpp                   # Report errors and warnings only\n";
    std::cout << "  " << programName << " check program.pycpp --error-format=json  # For editors\n";
    std::cout << "  " << programName << " program.pycpp --watch                 # Re-run on every save\n";
    std::cout << "  " << programName << " program.pycpp -v                      # Verbose output\n";
    std::cout << "  " << programName << " program.pycpp --keep-temp             # Keep temporary files\n";
    std::cout << "  " << programName << " program.pycpp --no-compile            # Generate C++ only\n";
    std::cout << "  " << programName << " program.pycpp --single-file           # Compile only one file\n";
    std::cout << "  " << programName << " program.pycpp -I ./lib               # With custom import path\n";
    std::cout << "  " << programName << " program.pycpp --compiler clang        # Build with clang++\n";
    std::cout << "  " << programName << " program.pycpp --compiler gcc          # Build with g++\n";
    std::cout << "  " << programName << " program.pycpp --compiler msvc         # Build with Visual Studio (cl.exe)\n";
    std::cout << "  " << programName << " game.pycpp --linker-flags \"-lraylib -lopengl32 -lgdi32 -lwinmm\"  # Link external libraries\n";
}

static int runPycpp(int argc, char* argv[]);

int main(int argc, char* argv[]) {
    const int code = runPycpp(argc, argv);
    pycpp::flushDiagnostics();
    return code;
}

static int runPycpp(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    Options opt;
    int first = 1;
    if (std::string(argv[1]) == "check") { opt.checkOnly = true; first = 2; }

    // value of `--name value` or `--name=value`
    auto valueOf = [&](int& i, const std::string& arg, const std::string& name, std::string& out) {
        if (arg.rfind(name + "=", 0) == 0) { out = arg.substr(name.size() + 1); return true; }
        if (arg != name) return false;
        if (i + 1 < argc) { out = argv[++i]; return true; }
        TerminalFormatter::printError("E0301", name + " option requires a value");
        out = "\x01";   // marks the error
        return true;
    };

    for (int i = first; i < argc; ++i) {
        std::string arg = argv[i];
        std::string value;
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        else if (valueOf(i, arg, "--explain", value)) {
            if (value == "\x01") return 1;
            if (!pycpp::explainCode(value)) {
                TerminalFormatter::printError("E0301", "no diagnostic has the code '" + value + "'",
                    "codes look like E0101 or W0001; they appear in brackets in every error and warning");
                return 1;
            }
            return 0;
        }
        else if (arg == "-v" || arg == "--verbose") {
            opt.verbose = true;
            pycpp::setVerbose(true);
        }
        else if (arg == "--keep-temp") opt.keepTemp = true;
        else if (arg == "-b" || arg == "--build") opt.buildOnly = true;
        else if (arg == "--no-compile") opt.noCompile = true;
        else if (arg == "--no-line-directives") opt.noLineDirectives = true;
        else if (arg == "--single-file") opt.singleFile = true;
        else if (arg == "--check") opt.checkOnly = true;
        else if (arg == "--watch") opt.watch = true;
        else if (arg == "--no-cache") opt.useCache = false;
        else if (valueOf(i, arg, "--error-format", value)) {
            if (value == "\x01") return 1;
            if (value == "human") pycpp::errorFormat() = pycpp::ErrorFormat::Human;
            else if (value == "short") pycpp::errorFormat() = pycpp::ErrorFormat::Short;
            else if (value == "json") pycpp::errorFormat() = pycpp::ErrorFormat::Json;
            else {
                TerminalFormatter::printError("E0301", "unknown error format '" + value + "'", "expected one of: human, short, json");
                return 1;
            }
        }
        else if (valueOf(i, arg, "--jobs", value) || (arg == "-j" && valueOf(i, arg, "-j", value)) ||
                 (arg.rfind("-j", 0) == 0 && arg.size() > 2 && std::isdigit(static_cast<unsigned char>(arg[2])) &&
                  (value = arg.substr(2), true))) {
            if (value == "\x01") return 1;
            try { opt.jobs = static_cast<unsigned>(std::stoul(value)); }
            catch (...) { opt.jobs = 0; }
            if (opt.jobs == 0) {
                TerminalFormatter::printError("E0301", "invalid number of jobs '" + value + "'", "use a positive number, e.g. -j 4");
                return 1;
            }
        }
        else if (valueOf(i, arg, "--compiler", value)) {
            if (value == "\x01") return 1;
            if (!CompilerBackend::parseKind(value, opt.compiler)) {
                TerminalFormatter::printError("E0301", "unknown compiler '" + value + "'", "expected one of: gcc, clang, msvc, auto");
                return 1;
            }
        }
        else if (valueOf(i, arg, "--linker-flags", value)) {
            if (value == "\x01") return 1;
            opt.linkerFlags = value;
        }
        else if (arg == "-I") {
            if (i + 1 < argc) opt.importPaths.push_back(argv[++i]);
            else {
                TerminalFormatter::printError("E0301", "-I option requires a directory path");
                return 1;
            }
        }
        else if (arg == "-o") {
            if (i + 1 < argc) opt.outputBase = argv[++i];
            else {
                TerminalFormatter::printError("E0301", "-o option requires an output filename");
                return 1;
            }
        }
        else if (!arg.empty() && arg[0] == '-') {
            static const std::vector<std::string> known_options = { "-h", "--help", "-v", "--verbose", "--keep-temp",
                "-b", "--build", "--no-compile", "--no-line-directives", "--single-file", "--compiler", "--linker-flags", "-I", "-o",
                "--check", "--watch", "--error-format", "--jobs", "--no-cache", "--explain" };
            const std::string guess = pycpp::closestMatch(arg, known_options);
            TerminalFormatter::printError("E0301", "unknown option '" + arg + "'",
                guess.empty() ? std::string("run `pycpp --help` to list the available options")
                              : "did you mean '" + guess + "'?");
            pycpp::flushDiagnostics();
            printUsage(argv[0]);
            return 1;
        }
        else opt.inputFile = arg;
    }

    if (opt.inputFile.empty()) {
        TerminalFormatter::printError("E0301", "no input file specified", "usage: pycpp <file.pycpp> [options]");
        pycpp::flushDiagnostics();
        printUsage(argv[0]);
        return 1;
    }

    if (opt.inputFile.length() < 7 || opt.inputFile.substr(opt.inputFile.length() - 6) != ".pycpp") {
        TerminalFormatter::printWarning("W0010", "input file '" + opt.inputFile + "' doesn't have the .pycpp extension",
                                        "PyCPP source files are named <name>.pycpp");
    }

    if (opt.outputBase.empty()) {
        const size_t lastDot = opt.inputFile.find_last_of('.');
        opt.outputBase = (lastDot != std::string::npos) ? opt.inputFile.substr(0, lastDot) : opt.inputFile;
    }

    if (opt.noCompile || opt.noLineDirectives)
        pycpp::lineDirectives() = false;
    const CompilerKind compiler = CompilerBackend::resolve(opt.compiler);
    if (opt.verbose) {
        TerminalFormatter::printSection("PyCPP Compiler");
        TerminalFormatter::printInfo("Input file: " + opt.inputFile);
        TerminalFormatter::printInfo("Output base: " + opt.outputBase);
        TerminalFormatter::printInfo("Mode: " + std::string(opt.checkOnly ? "Check only" : opt.singleFile ? "Single-file" : "Multi-file with auto-deps"));
        TerminalFormatter::printInfo("Compiler: " + CompilerBackend::displayName(compiler));
        if (!opt.linkerFlags.empty()) TerminalFormatter::printInfo("Linker flags: " + opt.linkerFlags);
    }

    if (!opt.watch) {
        const bool success = runOnce(opt, compiler);
        if (!success) return 1;
        return opt.checkOnly || opt.noCompile ? 0 : g_programExitCode;
    }

    // --watch: run, then wait for a source file to change, forever
    while (true) {
        runOnce(opt, compiler);
        pycpp::flushDiagnostics();
        std::cerr << "[watch] waiting for changes (Ctrl+C to stop)..." << std::endl;
        const auto before = sourceTimes(opt.inputFile);
        while (sourceTimes(opt.inputFile) == before) std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));   // let the editor finish writing
        resetState();
        std::cerr << "[watch] change detected, running again" << std::endl;
    }
}
