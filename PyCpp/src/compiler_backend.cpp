#include "../include/compiler_backend.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

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
const char* const kCppStandard = "17";

// Visual Studio environment script used when cl.exe is not already on PATH.
const char* const kVcVarsPath =
    "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat";

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
    if (n == "gcc" || n == "g++") { out = CompilerKind::Gcc; return true; }
    if (n == "msvc" || n == "cl") { out = CompilerKind::Msvc; return true; }
    return false;
}

CompilerKind CompilerBackend::resolve(CompilerKind requested) {
    if (requested != CompilerKind::Auto) return requested;
#ifdef _WIN32
    return CompilerKind::Msvc;
#else
    return CompilerKind::Gcc;
#endif
}

std::string CompilerBackend::displayName(CompilerKind kind) {
    switch (resolve(kind)) {
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
        command += " /I" + quoteArgWindows(request.includeDir);
        command += " /nologo";
    }
    else {
        command = std::string("g++ -std=c++") + kCppStandard;
        for (const auto& file : request.cppFiles) {
            command += " " + quoteArg(file);
        }
        command += " -o " + quoteArg(request.exeFile);
        command += " -I" + quoteArg(request.includeDir);
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
        "call \"" + std::string(kVcVarsPath) + "\" >nul 2>&1\r\n"
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
    case CompilerKind::Gcc:
        if (quietCommandSucceeds("g++ --version")) return true;
        reason = "g++ was not found on PATH. Install GCC (on Windows, MinGW-w64 or MSYS2) "
                 "or choose another compiler with --compiler msvc.";
        return false;

    case CompilerKind::Msvc:
#ifdef _WIN32
        if (quietCommandSucceeds("where cl")) return true;
        if (std::filesystem::exists(kVcVarsPath)) return true;
        reason = "Visual Studio 2022 (cl.exe) was not found. Run from a Developer Command "
                 "Prompt, install Visual Studio 2022 Community, or choose --compiler gcc.";
        return false;
#else
        reason = "MSVC (cl.exe) is only available on Windows. Use --compiler gcc.";
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
    return runCommand(quoteArg(exeFile) + " 2>&1", onOutput);
}
