#ifndef PYCPP_DIAGNOSTICS_H
#define PYCPP_DIAGNOSTICS_H

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <io.h>
#define PYCPP_ISATTY _isatty
#define PYCPP_FILENO _fileno
#else
#include <unistd.h>
#define PYCPP_ISATTY isatty
#define PYCPP_FILENO fileno
#endif

namespace pycpp {

enum class Severity { Error, Warning, Note };

struct Diagnostic {
    Severity severity = Severity::Error;
    std::string message;
    std::string hint;          // shown as "= help: ..."
    int line = 0, column = 0, length = 1;
};

class CompileError : public std::runtime_error {
public:
    std::vector<Diagnostic> diagnostics;
    explicit CompileError(std::vector<Diagnostic> d)
        : std::runtime_error(d.empty() ? "error" : d.front().message), diagnostics(std::move(d)) {}
    explicit CompileError(Diagnostic d) : CompileError(std::vector<Diagnostic>{std::move(d)}) {}
};

struct SourceFile { std::string name; std::vector<std::string> lines; };
inline SourceFile& source() { static SourceFile s; return s; }
inline void setSource(const std::string& name, const std::string& text) {
    SourceFile& s = source();
    s.name = name;
    s.lines.clear();
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        s.lines.push_back(l);
    }
}

inline bool useColor() {
    static const bool on = [] {
        if (std::getenv("NO_COLOR") || !PYCPP_ISATTY(PYCPP_FILENO(stderr))) return false;
#ifdef _WIN32
        return std::getenv("WT_SESSION") != nullptr || std::getenv("ANSICON") != nullptr;
#else
        return true;
#endif
    }();
    return on;
}
inline std::string paint(const char* code, const std::string& text) {
    return useColor() ? std::string("\x1b[") + code + "m" + text + "\x1b[0m" : text;
}

//  error: cannot find 'scor' in this scope
//    --> game.pycpp:12:9
//     |
//  12 |     scor += 1
//     |     ^^^^
//     = help: did you mean 'score'?
inline std::string render(const Diagnostic& d) {
    const char* label = d.severity == Severity::Error ? "error"
                      : d.severity == Severity::Warning ? "warning" : "note";
    const char* color = d.severity == Severity::Error ? "1;31"
                      : d.severity == Severity::Warning ? "1;33" : "1;36";
    std::ostringstream out;
    out << paint(color, label) << paint("1", ": " + d.message) << "\n";

    const SourceFile& src = source();
    const std::string num = d.line > 0 ? std::to_string(d.line) : "";
    const std::string pad(num.size(), ' ');
    if (d.line > 0) {
        out << pad << paint("1;34", "--> ") << src.name << ":" << d.line << ":" << d.column << "\n";
        if (d.line <= static_cast<int>(src.lines.size())) {
            const std::string& text = src.lines[d.line - 1];
            out << pad << paint("1;34", " |") << "\n";
            out << paint("1;34", num + " | ") << text << "\n";
            out << pad << paint("1;34", " | ");
            for (int i = 0; i < d.column - 1 && i < static_cast<int>(text.size()); ++i)
                out << (text[i] == '\t' ? '\t' : ' ');
            out << paint(color, std::string(std::max(d.length, 1), '^')) << "\n";
        }
    }
    if (!d.hint.empty()) out << pad << paint("1;34", " = ") << paint("1", "help: ") << d.hint << "\n";
    return out.str();
}
inline void emit(const Diagnostic& d) { std::cerr << render(d) << "\n"; }

inline size_t editDistance(const std::string& a, const std::string& b) {
    std::vector<size_t> row(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
        size_t diag = row[0];
        row[0] = i;
        for (size_t j = 1; j <= b.size(); ++j) {
            size_t up = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diag = up;
        }
    }
    return row[b.size()];
}
inline std::string closestMatch(const std::string& word, const std::vector<std::string>& candidates) {
    std::string best;
    size_t best_d = std::max<size_t>(1, word.size() / 3) + 1;
    for (const auto& c : candidates) {
        size_t d = editDistance(word, c);
        if (d < best_d) { best_d = d; best = c; }
    }
    return best;
}

} // namespace pycpp
#endif
