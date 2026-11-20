#ifndef PYCPP_DIAGNOSTICS_H
#define PYCPP_DIAGNOSTICS_H
// [diag-fix] Rust-style diagnostics:
//
//  error[E0001]: cannot find 'scor' in this scope
//    --> game.pycpp:12:9
//     |
//  11 |     score: int = 0
//  12 |     scor += 1
//     |     ^^^^ not found
//     |
//     = note: names must be declared before they are used
//     = help: did you mean 'score'?

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
    std::string code;                       // optional, shown as error[E0001]
    std::string message;                    // one-line summary
    std::string label;                      // short text printed beside the carets
    std::string hint;                       // "= help: ..."
    std::vector<std::string> notes;         // "= note: ..." (may contain newlines)
    int line = 0, column = 0, length = 1;
    std::string file;                       // overrides the current source file name
    std::vector<std::string> snippet_lines; // overrides the current source text
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

inline int& warningCount() { static int n = 0; return n; }

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

inline std::string expandTabs(const std::string& s) {
    std::string out;
    for (char c : s) { if (c == '\t') out += "    "; else out += c; }
    return out;
}
// Width on screen of the first `chars` characters (a tab is shown as 4 spaces).
inline size_t visualOffset(const std::string& text, size_t chars) {
    size_t v = 0;
    for (size_t i = 0; i < chars && i < text.size(); ++i) v += text[i] == '\t' ? 4 : 1;
    return v;
}
inline bool isBlank(const std::string& s) {
    return s.find_first_not_of(" \t\r") == std::string::npos;
}

inline std::string render(const Diagnostic& d) {
    const char* label = d.severity == Severity::Error ? "error"
                      : d.severity == Severity::Warning ? "warning" : "note";
    const char* color = d.severity == Severity::Error ? "1;31"
                      : d.severity == Severity::Warning ? "1;33" : "1;36";
    std::ostringstream out;
    std::string head = label;
    if (!d.code.empty()) head += "[" + d.code + "]";
    out << paint(color, head) << paint("1", ": " + d.message) << "\n";

    const SourceFile& src = source();
    const std::vector<std::string>& lines = d.snippet_lines.empty() ? src.lines : d.snippet_lines;
    const std::string& fname = d.file.empty() ? src.name : d.file;

    const std::string num = d.line > 0 ? std::to_string(d.line) : "";
    const std::string pad(num.size(), ' ');
    const std::string gutter = paint("1;34", " |");
    bool framed = false;

    if (d.line > 0) {
        out << pad << paint("1;34", "--> ") << fname << ":" << d.line << ":" << d.column << "\n";
        if (d.line <= static_cast<int>(lines.size())) {
            framed = true;
            out << pad << gutter << "\n";
            if (d.line > 1 && !isBlank(lines[d.line - 2])) {            // one line of context
                const std::string prev = std::to_string(d.line - 1);
                out << paint("1;34", std::string(num.size() - prev.size(), ' ') + prev + " | ")
                    << paint("2", expandTabs(lines[d.line - 2])) << "\n";
            }
            const std::string& text = lines[d.line - 1];
            out << paint("1;34", num + " | ") << expandTabs(text) << "\n";

            size_t start = d.column > 0 ? static_cast<size_t>(d.column - 1) : 0;
            if (start > text.size()) start = text.size();
            const size_t end = std::min(text.size(), start + static_cast<size_t>(std::max(d.length, 1)));
            const size_t vstart = visualOffset(text, start);
            const size_t vlen = std::max<size_t>(1, visualOffset(text, end) - vstart);
            out << pad << gutter << " " << std::string(vstart, ' ') << paint(color, std::string(vlen, '^'));
            if (!d.label.empty()) out << " " << paint(color, d.label);
            out << "\n";
        }
    }

    if (framed && (!d.notes.empty() || !d.hint.empty())) out << pad << gutter << "\n";

    auto extra = [&](const char* tag, const std::string& text) {
        std::istringstream in(text);
        std::string l;
        bool first = true;
        const std::string indent(std::string(tag).size() + 2, ' ');
        while (std::getline(in, l)) {
            if (first) out << pad << paint("1;34", " = ") << paint("1", tag) << ": " << l << "\n";
            else out << pad << "   " << indent << l << "\n";
            first = false;
        }
    };
    for (const auto& n : d.notes) extra("note", n);
    if (!d.hint.empty()) extra("help", d.hint);
    return out.str();
}
inline void emit(const Diagnostic& d) {
    if (d.severity == Severity::Warning) ++warningCount();
    std::cerr << render(d) << "\n";
}

inline std::string failureSummary(size_t errors) {
    std::string s = paint("1;31", "error") +
        paint("1", ": could not compile due to " + std::to_string(errors) + (errors == 1 ? " error" : " errors"));
    const int w = warningCount();
    if (w > 0) s += paint("1", "; " + std::to_string(w) + (w == 1 ? " warning" : " warnings") + " emitted");
    return s;
}

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
