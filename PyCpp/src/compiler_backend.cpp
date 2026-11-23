#include "../include/compiler_backend.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#define PYCPP_POPEN _popen
#define PYCPP_PCLOSE _pclose
#else
#include <sys/wait.h>
#define PYCPP_POPEN popen
#define PYCPP_PCLOSE pclose
#endif

namespace {

// C++ language standard passed to both compilers, so gcc and MSVC build the
// generated code the same way.
const char* const kCppStandard = "20";

// Visual Studio environment script used when cl.exe is not already on PATH.
// [fix2] Visual Studio environment script: PYCPP_VCVARS overrides it, otherwise the usual install paths are searched.
std::string vcVarsPath() {
    if (const char* custom = std::getenv("PYCPP_VCVARS")) return custom;
    const char* roots[] = { "C:\\Program Files\\Microsoft Visual Studio", "C:\\Program Files (x86)\\Microsoft Visual Studio" };
    const char* years[] = { "2022", "2019", "2017" };
    const char* editions[] = { "Community", "Professional", "Enterprise", "BuildTools" };
    for (const char* root : roots)
        for (const char* year : years)
            for (const char* edition : editions) {
                const std::string candidate = std::string(root) + "\\" + year + "\\" + edition +
                                              "\\VC\\Auxiliary\\Build\\vcvars64.bat";
                std::error_code ec;
                if (std::filesystem::exists(candidate, ec)) return candidate;
            }
    return std::string(roots[0]) + "\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat";
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Quote one command-line argument for the shell that popen() uses.
std::string quoteArg(const std::string& s) {
#ifdef _WIN32
    return "\"" + s + "\"";
#else
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
#endif
}

// MSVC is always driven through cmd.exe, so its arguments always use double quotes.
std::string quoteArgWindows(const std::string& s) {
    return "\"" + s + "\"";
}

// Convert a pclose()/system() status into the child's exit code.
int exitCodeFromStatus(int status) {
    if (status == -1) return -1;
#ifdef _WIN32
    return status;
#else
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
#endif
}

// Run a shell command and pass its combined output to `onOutput` line by line.
// Returns the exit code, or -1 if the command could not be started.
int runCommand(const std::string& command,
    const std::function<void(const std::string&)>& onOutput) {
    FILE* pipe = PYCPP_POPEN(command.c_str(), "r");
    if (!pipe) return -1;

    std::string line;
    char buffer[4096];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        line += buffer;
        if (!line.empty() && line.back() == '\n') {
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                line.pop_back();
            }
            if (onOutput) onOutput(line);
            line.clear();
        }
    }
    if (!line.empty() && onOutput) onOutput(line); // last line without newline

    return exitCodeFromStatus(PYCPP_PCLOSE(pipe));
}

bool quietCommandSucceeds(const std::string& command) {
#ifdef _WIN32
    const std::string quiet = command + " >nul 2>&1";
#else
    const std::string quiet = command + " >/dev/null 2>&1";
#endif
    return exitCodeFromStatus(std::system(quiet.c_str())) == 0;
}

} // namespace

bool CompilerBackend::parseKind(const std::string& name, CompilerKind& out) {
    const std::string n = toLower(name);
    if (n == "auto") { out = CompilerKind::Auto; return true; }
    if (n == "clang" || n == "clang++") { out = CompilerKind::Clang; return true; }
    if (n == "gcc" || n == "g++") { out = CompilerKind::Gcc; return true; }
    if (n == "msvc" || n == "cl") { out = CompilerKind::Msvc; return true; }
    return false;
}

CompilerKind CompilerBackend::resolve(CompilerKind requested) {
    if (requested != CompilerKind::Auto) return requested;
#ifdef _WIN32
    return CompilerKind::Msvc;
#elif defined(__APPLE__)
    return CompilerKind::Clang;
#else
    return CompilerKind::Gcc;
#endif
}

std::string CompilerBackend::displayName(CompilerKind kind) {
    switch (resolve(kind)) {
    case CompilerKind::Clang: return "Clang (clang++)";
    case CompilerKind::Gcc:  return "gcc (g++)";
    case CompilerKind::Msvc: return "MSVC (cl.exe)";
    default:                 return "auto";
    }
}

std::string CompilerBackend::exeExtension(CompilerKind kind) {
    if (resolve(kind) == CompilerKind::Msvc) return ".exe";
#ifdef _WIN32
    return ".exe"; // MinGW also produces .exe
#else
    return "";
#endif
}

std::string CompilerBackend::buildCompileCommand(CompilerKind kind, const CompileRequest& request) {
    std::string command;

    if (resolve(kind) == CompilerKind::Msvc) {
        command = std::string("cl /EHsc /std:c++") + kCppStandard;
        for (const auto& file : request.cppFiles) {
            command += " " + quoteArgWindows(file);
        }
        command += " /Fe" + quoteArgWindows(request.exeFile);
        // no /I: a quoted include is found next to the including file, and
        // MSVC has no quote-only variant of /I
        if (request.optimize) command += " /O2";
        command += " /nologo";
    }
    else {
        command = std::string(resolve(kind) == CompilerKind::Clang ? "clang++ -std=c++" : "g++ -std=c++") + kCppStandard;
        for (const auto& file : request.cppFiles) {
            command += " " + quoteArg(file);
        }
        command += " -o " + quoteArg(request.exeFile);
        // -iquote only affects #include "..."; plain -I let temp/assert.h hijack <cassert>
        command += " -iquote " + quoteArg(request.includeDir);
        if (request.optimize) command += " -O2";
        command += " " + request.linkerFlags;
        
    }

    return command;
}

std::string CompilerBackend::buildMsvcBatch(const CompileRequest& request) {
    // Use cl.exe as-is when it is already on PATH (e.g. a Developer Command
    // Prompt); otherwise load the Visual Studio environment first.
    return
        "@echo off\r\n"
        "where cl >nul 2>&1\r\n"
        "if %errorlevel% equ 0 goto compile\r\n"
        "call \"" + vcVarsPath() + "\" >nul 2>&1\r\n"
        "if %errorlevel% neq 0 (\r\n"
        "  echo Failed to set up the Visual Studio environment\r\n"
        "  exit /b 1\r\n"
        ")\r\n"
        ":compile\r\n" +
        buildCompileCommand(CompilerKind::Msvc, request) + "\r\n"
        "exit /b %errorlevel%\r\n";
}

bool CompilerBackend::isAvailable(CompilerKind kind, std::string& reason) {
    switch (resolve(kind)) {
    case CompilerKind::Clang:
        if (quietCommandSucceeds("clang++ --version")) return true;
        reason = "clang++ was not found on PATH. On macOS, install Apple Command Line Tools with xcode-select --install.";
        return false;

    case CompilerKind::Gcc:
        if (quietCommandSucceeds("g++ --version")) return true;
        reason = "g++ was not found on PATH. Install GCC (on Windows, MinGW-w64 or MSYS2) "
                 "or choose another compiler with --compiler msvc.";
        return false;

    case CompilerKind::Msvc:
#ifdef _WIN32
        if (quietCommandSucceeds("where cl")) return true;
        if (std::filesystem::exists(vcVarsPath())) return true;
        reason = "Visual Studio 2022 (cl.exe) was not found. Run from a Developer Command "
                 "Prompt, install Visual Studio 2022 Community, or choose --compiler gcc.";
        return false;
#else
        reason = "MSVC (cl.exe) is only available on Windows. Use --compiler clang or --compiler gcc.";
        return false;
#endif

    default:
        reason = "no compiler selected";
        return false;
    }
}

bool CompilerBackend::compile(CompilerKind kind, const CompileRequest& request, bool verbose,
    const std::function<void(const std::string&)>& onOutput) {
    const CompilerKind resolved = resolve(kind);

    // Remove a stale executable so success is judged on this build only.
    std::error_code ec;
    std::filesystem::remove(request.exeFile, ec);

    int code = -1;

    if (resolved == CompilerKind::Msvc) {
        const std::string batchFile = request.includeDir + "\\compile_msvc.bat";
        {
            std::ofstream batch(batchFile, std::ios::binary);
            if (!batch) {
                if (onOutput) onOutput("Could not create build script: " + batchFile);
                return false;
            }
            batch << buildMsvcBatch(request);
        }
        if (verbose && onOutput) onOutput("Executing: " + buildCompileCommand(kind, request));
        code = runCommand("cmd /c \"" + batchFile + "\" 2>&1", onOutput);
        std::filesystem::remove(batchFile, ec);
    }
    else {
        const std::string command = buildCompileCommand(kind, request);
        if (verbose && onOutput) onOutput("Executing: " + command);
        code = runCommand(command + " 2>&1", onOutput);
    }

    return code == 0 && std::filesystem::exists(request.exeFile);
}

int CompilerBackend::run(const std::string& exeFile,
    const std::function<void(const std::string&)>& onOutput) {
    return exitCodeFromStatus(std::system(quoteArg(exeFile).c_str()));
}

// ===== Incremental, parallel builds =====

std::string CompilerBackend::objectExtension(CompilerKind kind) {
    return resolve(kind) == CompilerKind::Msvc ? ".obj" : ".o";
}

std::string CompilerBackend::buildObjectCommand(CompilerKind kind, const std::string& cppFile,
    const std::string& objFile, const std::string& includeDir, bool optimize) {
    if (resolve(kind) == CompilerKind::Msvc) {
        return std::string("cl /c /EHsc /std:c++") + kCppStandard + " " + quoteArgWindows(cppFile) +
               " /Fo" + quoteArgWindows(objFile) + (optimize ? " /O2" : "") + " /nologo";
    }
    std::string command = std::string(resolve(kind) == CompilerKind::Clang ? "clang++ -std=c++" : "g++ -std=c++") +
                          kCppStandard + " -c " + quoteArg(cppFile) + " -o " + quoteArg(objFile) +
                          " -iquote " + quoteArg(includeDir);
    if (optimize) command += " -O2";
    return command;
}

std::string CompilerBackend::buildLinkCommand(CompilerKind kind, const std::vector<std::string>& objFiles,
    const std::string& exeFile, const std::string& linkerFlags) {
    std::string command;
    if (resolve(kind) == CompilerKind::Msvc) {
        command = "cl /nologo";
        for (const auto& obj : objFiles) command += " " + quoteArgWindows(obj);
        command += " /Fe" + quoteArgWindows(exeFile);
        if (!linkerFlags.empty()) command += " " + linkerFlags;
        return command;
    }
    command = resolve(kind) == CompilerKind::Clang ? "clang++" : "g++";
    for (const auto& obj : objFiles) command += " " + quoteArg(obj);
    command += " -o " + quoteArg(exeFile);
    if (!linkerFlags.empty()) command += " " + linkerFlags;
    return command;
}

bool CompilerBackend::build(CompilerKind kind, const BuildRequest& request, bool verbose,
    const std::function<void(const std::string&)>& onOutput, std::vector<bool>& compiled) {
    const CompilerKind resolved = resolve(kind);
    compiled.assign(request.units.size(), false);
    std::error_code ec;
    std::filesystem::remove(request.exeFile, ec);
    for (const auto& unit : request.units)
        if (unit.compile) std::filesystem::remove(unit.objFile, ec);   // judged on this build only

    std::vector<std::string> objects;
    for (const auto& unit : request.units) objects.push_back(unit.objFile);
    const std::string link = buildLinkCommand(kind, objects, request.exeFile, request.linkerFlags);

    if (resolved == CompilerKind::Msvc) {
        // One batch: set up Visual Studio once, compile every stale unit in a single
        // `cl /MP` call (cl compiles the files of one call in parallel), then link.
        // Object files are named after their source, in the directory given by /Fo.
        std::string compile_all;
        std::map<std::string, std::vector<std::string>> by_dir;   // object directory -> sources
        for (const auto& unit : request.units) {
            if (!unit.compile) continue;
            const std::string dir = std::filesystem::path(unit.objFile).parent_path().string();
            by_dir[dir].push_back(unit.cppFile);
        }
        for (const auto& [dir, sources] : by_dir) {
            compile_all += "cl /c /MP" + std::to_string(std::max(1u, request.jobs)) + " /EHsc /std:c++" + std::string(kCppStandard);
            for (const auto& src : sources) compile_all += " " + quoteArgWindows(src);
            // "dir\\" : a backslash before the closing quote must be doubled for cl's argument parser
            compile_all += " /Fo\"" + dir + "\\\\\"" + (request.optimize ? " /O2" : "") + " /nologo\r\n"
                           "if %errorlevel% neq 0 set PYCPP_FAILED=1\r\n";
        }
        const std::string batchFile = request.includeDir + "\\build_msvc.bat";
        {
            std::ofstream batch(batchFile, std::ios::binary);
            if (!batch) {
                if (onOutput) onOutput("Could not create build script: " + batchFile);
                return false;
            }
            batch << "@echo off\r\n"
                     "set PYCPP_FAILED=0\r\n"
                     "where cl >nul 2>&1\r\n"
                     "if %errorlevel% equ 0 goto compile\r\n"
                     "call \"" << vcVarsPath() << "\" >nul 2>&1\r\n"
                     "if %errorlevel% neq 0 (\r\n"
                     "  echo Failed to set up the Visual Studio environment\r\n"
                     "  exit /b 1\r\n"
                     ")\r\n"
                     ":compile\r\n"
                  << compile_all
                  << "if %PYCPP_FAILED% neq 0 exit /b 1\r\n"
                  << link << "\r\n"
                     "exit /b %errorlevel%\r\n";
        }
        if (verbose && onOutput) onOutput("Executing: " + batchFile);
        const int code = runCommand("cmd /c \"" + batchFile + "\" 2>&1", onOutput);
        std::filesystem::remove(batchFile, ec);
        for (size_t i = 0; i < request.units.size(); ++i)
            compiled[i] = request.units[i].compile && std::filesystem::exists(request.units[i].objFile);
        return code == 0 && std::filesystem::exists(request.exeFile);
    }

    // g++ / clang++: up to `jobs` compiler processes at once; each unit's output is kept
    // together and reported in module order.
    std::vector<size_t> pending;
    for (size_t i = 0; i < request.units.size(); ++i) if (request.units[i].compile) pending.push_back(i);
    std::vector<std::vector<std::string>> logs(request.units.size());
    std::vector<int> codes(request.units.size(), 0);
    std::mutex next_mutex;
    size_t next = 0;
    auto worker = [&] {
        while (true) {
            size_t index;
            {
                std::lock_guard<std::mutex> lock(next_mutex);
                if (next >= pending.size()) return;
                index = pending[next++];
            }
            const BuildUnit& unit = request.units[index];
            const std::string command = buildObjectCommand(kind, unit.cppFile, unit.objFile, request.includeDir, request.optimize);
            if (verbose) logs[index].push_back("Executing: " + command);
            codes[index] = runCommand(command + " 2>&1", [&](const std::string& line) { logs[index].push_back(line); });
        }
    };
    const unsigned workers = std::max(1u, std::min<unsigned>(request.jobs, static_cast<unsigned>(pending.size())));
    std::vector<std::thread> threads;
    for (unsigned t = 1; t < workers; ++t) threads.emplace_back(worker);
    worker();
    for (auto& t : threads) t.join();

    bool ok = true;
    for (size_t i = 0; i < request.units.size(); ++i) {
        if (!request.units[i].compile) continue;
        if (onOutput) for (const auto& line : logs[i]) onOutput(line);
        compiled[i] = codes[i] == 0 && std::filesystem::exists(request.units[i].objFile);
        if (!compiled[i]) ok = false;
    }
    if (!ok) return false;

    if (verbose && onOutput) onOutput("Executing: " + link);
    const int code = runCommand(link + " 2>&1", onOutput);
    return code == 0 && std::filesystem::exists(request.exeFile);
}
