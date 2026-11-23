#ifndef PYCPP_DIAGNOSTICS_H
#define PYCPP_DIAGNOSTICS_H
// [diag-fix] Rust-style diagnostics:
//
//  error[E0101]: cannot find 'scor' in this scope
//    --> game.pycpp:12:9
//     |
//  11 |     score: int = 0
//  12 |     scor += 1
//     |     ^^^^ not found
//     |
//     = note: names must be declared before they are used
//     = help: did you mean 'score'?
//
// Every diagnostic goes through emit(), which only queues it. flushDiagnostics() prints
// the queue sorted by file, then line, then column, so warnings and errors from the
// same run read top to bottom. `# allow: <name>` comments silence warnings per line.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "error_codes.h"
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
    std::string code;                       // stable code (see error_codes.h), shown as error[E0101]
    std::string message;                    // one-line summary
    std::string label;                      // short text printed beside the carets
    std::string hint;                       // "= help: ..."
    std::vector<std::string> notes;         // "= note: ..." (may contain newlines)
    int line = 0, column = 0, length = 1;   // length: carets on `line` (clamped to the line end)
    std::string file;                       // source file; filled in by emit() when empty
    std::vector<std::string> snippet_lines; // overrides the registered source text
};

class CompileError : public std::runtime_error {
public:
    std::vector<Diagnostic> diagnostics;
    explicit CompileError(std::vector<Diagnostic> d)
        : std::runtime_error(d.empty() ? "error" : d.front().message), diagnostics(std::move(d)) {}
    explicit CompileError(Diagnostic d) : CompileError(std::vector<Diagnostic>{std::move(d)}) {}
};

// ---- source text of every file seen in this run (for code frames and `# allow:`) ----

struct SourceFile { std::string name; std::vector<std::string> lines; };
inline SourceFile& source() { static SourceFile s; return s; }
inline std::map<std::string, std::vector<std::string>>& sourceRegistry() {
    static std::map<std::string, std::vector<std::string>> files;
    return files;
}
inline std::vector<std::string>& sourceOrder() { static std::vector<std::string> order; return order; }

inline std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
    }
    return lines;
}

inline void setSource(const std::string& name, const std::string& text) {
    SourceFile& s = source();
    s.name = name;
    s.lines = splitLines(text);
    if (!sourceRegistry().count(name)) sourceOrder().push_back(name);
    sourceRegistry()[name] = s.lines;
}

// Lines of `file` (the current source when file is empty or unknown).
inline const std::vector<std::string>& linesOf(const std::string& file) {
    if (!file.empty()) {
        auto it = sourceRegistry().find(file);
        if (it != sourceRegistry().end()) return it->second;
    }
    return source().lines;
}

// ---- output format ----

enum class ErrorFormat { Human, Short, Json };
inline ErrorFormat& errorFormat() { static ErrorFormat f = ErrorFormat::Human; return f; }

inline int& warningCount() { static int n = 0; return n; }

inline bool useColor() {
    static const bool on = [] {
        if (std::getenv("NO_COLOR") || !PYCPP_ISATTY(PYCPP_FILENO(stderr))) return false;
#ifdef _WIN32
        return std::getenv("WT_SESSION") != nullptr || std::getenv("ANSICON") != nullptr ||
               std::getenv("TERM") != nullptr || std::getenv("PYCPP_COLOR") != nullptr;   // [fix2]
#else
        return true;
#endif
    }();
    return on && errorFormat() == ErrorFormat::Human;
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

// Caret range [start, end) on the diagnostic's line, clamped to the text.
inline void caretRange(const Diagnostic& d, const std::string& text, size_t& start, size_t& end) {
    start = d.column > 0 ? static_cast<size_t>(d.column - 1) : 0;
    if (start > text.size()) start = text.size();
    end = std::min(text.size(), start + static_cast<size_t>(std::max(d.length, 1)));
    if (end < start) end = start;
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

    const std::vector<std::string>& lines = d.snippet_lines.empty() ? linesOf(d.file) : d.snippet_lines;
    const std::string& fname = d.file.empty() ? source().name : d.file;

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

            size_t start = 0, end = 0;
            caretRange(d, text, start, end);
            const size_t vstart = visualOffset(text, start);
            const size_t vlen = std::max<size_t>(1, visualOffset(text, end) - vstart);
            out << pad << gutter << " " << std::string(vstart, ' ') << paint(color, std::string(vlen, '^'));
            if (!d.label.empty()) out << " " << paint(color, d.label);
            out << "\n";
        }
    }
    else if (!d.file.empty()) {
        out << paint("1;34", "--> ") << d.file << "\n";
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

inline std::string jsonEscape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            }
            else out += static_cast<char>(c);
        }
    }
    return out;
}

inline const char* severityName(Severity s) {
    return s == Severity::Error ? "error" : s == Severity::Warning ? "warning" : "note";
}

// One JSON object per line. "message" is the last key, so editors can also match the
// line with a simple regular expression (see .vscode/tasks.json).
inline std::string renderJson(const Diagnostic& d) {
    int end_column = d.column;
    if (d.line > 0) {
        const auto& lines = d.snippet_lines.empty() ? linesOf(d.file) : d.snippet_lines;
        if (d.line <= static_cast<int>(lines.size())) {
            size_t start = 0, end = 0;
            caretRange(d, lines[d.line - 1], start, end);
            end_column = static_cast<int>(std::max(end, start + 1)) + 1;
        }
        else end_column = d.column + std::max(d.length, 1);
    }
    const CodeInfo* info = d.code.empty() ? nullptr : findCode(d.code);
    std::ostringstream o;
    o << "{\"file\":\"" << jsonEscape(d.file) << "\""
      << ",\"line\":" << d.line << ",\"column\":" << d.column
      << ",\"end_line\":" << d.line << ",\"end_column\":" << end_column
      << ",\"severity\":\"" << severityName(d.severity) << "\""
      << ",\"code\":\"" << jsonEscape(d.code) << "\""
      << ",\"name\":\"" << (info ? info->name : "") << "\""
      << ",\"label\":\"" << jsonEscape(d.label) << "\""
      << ",\"hint\":\"" << jsonEscape(d.hint) << "\""
      << ",\"notes\":[";
    for (size_t i = 0; i < d.notes.size(); ++i) o << (i ? "," : "") << "\"" << jsonEscape(d.notes[i]) << "\"";
    o << "],\"message\":\"" << jsonEscape(d.message) << "\"}";
    return o.str();
}

// file:line:column: error[E0101]: message   (the format of gcc and most editors)
inline std::string renderShort(const Diagnostic& d) {
    std::ostringstream o;
    if (!d.file.empty()) {
        o << d.file << ":";
        if (d.line > 0) o << d.line << ":" << std::max(d.column, 1) << ":";
        o << " ";
    }
    o << severityName(d.severity);
    if (!d.code.empty()) o << "[" << d.code << "]";
    o << ": " << d.message;
    if (!d.hint.empty()) o << " (help: " << d.hint << ")";
    return o.str();
}

// ---- `# allow: name` ----

// The text of a '#' comment on the line, ignoring '#' inside string literals.
inline std::string commentOf(const std::string& line) {
    char quote = '\0';
    bool escaped = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quote) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == quote) quote = '\0';
        }
        else if (c == '"' || c == '\'') quote = c;
        else if (c == '#') return line.substr(i + 1);
    }
    return "";
}

// Does the comment contain `allow: <name|code|all>` naming this warning?
inline bool commentAllows(const std::string& comment, const CodeInfo* info) {
    const size_t at = comment.find("allow:");
    if (at == std::string::npos) return false;
    std::string list = comment.substr(at + 6);
    for (char& c : list) if (c == ',' || c == ';') c = ' ';
    std::istringstream words(list);
    for (std::string w; words >> w;) {
        std::string lower = w;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == "all") return true;
        if (info && (lower == info->name || findCode(w) == info)) return true;
    }
    return false;
}

inline bool isAllowed(const Diagnostic& d) {
    if (d.severity != Severity::Warning || d.line <= 0 || d.code.empty()) return false;
    const CodeInfo* info = findCode(d.code);
    const auto& lines = linesOf(d.file);
    if (d.line > static_cast<int>(lines.size())) return false;
    if (commentAllows(commentOf(lines[d.line - 1]), info)) return true;
    // a comment line of its own directly above the line
    for (int l = d.line - 1; l >= 1; --l) {
        const std::string& text = lines[l - 1];
        const size_t first = text.find_first_not_of(" \t");
        if (first == std::string::npos || text[first] != '#') break;
        if (commentAllows(text.substr(first + 1), info)) return true;
    }
    return false;
}

// ---- the queue ----

inline std::vector<Diagnostic>& pendingDiagnostics() { static std::vector<Diagnostic> q; return q; }

inline void emit(const Diagnostic& diagnostic) {
    Diagnostic d = diagnostic;
    if (d.file.empty() && d.line > 0) d.file = source().name;
    if (isAllowed(d)) return;
    for (const auto& seen : pendingDiagnostics())       // the same problem found twice
        if (seen.severity == d.severity && seen.file == d.file && seen.line == d.line &&
            seen.column == d.column && seen.message == d.message) return;
    if (d.severity == Severity::Warning) ++warningCount();
    pendingDiagnostics().push_back(std::move(d));
}

// Print everything queued, sorted by file (in the order the files were read), line and
// column. Diagnostics without a position keep their order and come after their file's.
inline void flushDiagnostics() {
    auto& q = pendingDiagnostics();
    if (q.empty()) return;
    auto fileIndex = [](const Diagnostic& d) -> size_t {
        const auto& order = sourceOrder();
        for (size_t i = 0; i < order.size(); ++i) if (order[i] == d.file) return i;
        return d.file.empty() ? order.size() + 1 : order.size();
    };
    std::stable_sort(q.begin(), q.end(), [&](const Diagnostic& a, const Diagnostic& b) {
        const size_t fa = fileIndex(a), fb = fileIndex(b);
        if (fa != fb) return fa < fb;
        const int la = a.line > 0 ? a.line : 1 << 30, lb = b.line > 0 ? b.line : 1 << 30;
        if (la != lb) return la < lb;
        return a.column < b.column;
    });
    for (const auto& d : q) {
        switch (errorFormat()) {
        case ErrorFormat::Json:  std::cerr << renderJson(d) << "\n"; break;
        case ErrorFormat::Short: std::cerr << renderShort(d) << "\n"; break;
        default:                 std::cerr << render(d) << "\n"; break;
        }
    }
    std::cerr.flush();
    q.clear();
}

// Final "could not compile" line. It is part of the human format only.
inline void printFailureSummary(size_t errors) {
    flushDiagnostics();
    if (errorFormat() != ErrorFormat::Human) return;
    std::string s = paint("1;31", "error") +
        paint("1", ": could not compile due to " + std::to_string(errors) + (errors == 1 ? " error" : " errors"));
    const int w = warningCount();
    if (w > 0) s += paint("1", "; " + std::to_string(w) + (w == 1 ? " warning" : " warnings") + " emitted");
    std::cerr << s << "\n";
}

// Forget everything about the previous run (used by --watch).
inline void resetDiagnostics() {
    pendingDiagnostics().clear();
    warningCount() = 0;
    sourceRegistry().clear();
    sourceOrder().clear();
    source() = SourceFile{};
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

// `pycpp --explain E0101`
inline bool explainCode(const std::string& key) {
    const CodeInfo* info = findCode(key);
    if (!info) return false;
    std::cout << info->code << " (" << info->name << "): " << info->title << "\n\n" << info->explanation << "\n";
    return true;
}

} // namespace pycpp
#endif
