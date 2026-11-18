#!/usr/bin/env python3
"""
fixes.py -- strips the `unsafe:` block from PyCPP entirely.

Run from the repo root:
    python fixes.py            # apply
    python fixes.py --check    # dry run
"""
import sys
from pathlib import Path

ROOT = Path(".")
CHECK = "--check" in sys.argv
EDITS = []


def edit(path, mode, anchor, new, end=None):
    EDITS.append((path, mode, anchor, new, end))


# ---------------------------------------------------------- semantic_analyzer.cpp
SA = "PyCpp/src/semantic_analyzer.cpp"

edit(SA, "replace",
     '''        if (call->is_subscript && unsafe_depth == 0) {
            if (auto ptr_ident = dynamic_cast<Identifier*>(call->callee.get())) {
                Symbol* sym = findSymbol(ptr_ident->name);
                if (sym && !sym->type.empty() && sym->type.back() == '*')
                    error("Indexing pointer '" + ptr_ident->name + "' requires an 'unsafe:' block",
                        call->line, call->column);
            }
        }
''',
     "")

edit(SA, "replace",
     '''        if (auto obj = dynamic_cast<Identifier*>(member_access->object.get())) {
            Symbol* sym = findSymbol(obj->name);
            if (sym && !sym->type.empty() && sym->type.back() == '*' && unsafe_depth == 0)
                error("Accessing '" + member_access->member + "' through pointer '" + obj->name +
                    "' requires an 'unsafe:' block", member_access->line, member_access->column);
        }
''',
     "")

edit(SA, "replace",
     '''        if (unary_op->op == "*" && unsafe_depth == 0)
            error("Pointer dereference requires an 'unsafe:' block", unary_op->line, unary_op->column);
''',
     "")

edit(SA, "replace",
     '''    else if (auto unsafe_block = dynamic_cast<UnsafeBlock*>(stmt)) {
        unsafe_depth++;
        enterScope();
        for (auto& inner : unsafe_block->body) {
            analyzeStatement(inner.get());
        }
        exitScope();
        unsafe_depth--;
    }''',
     '''    else if (auto unsafe_block = dynamic_cast<UnsafeBlock*>(stmt)) {
        enterScope();
        for (auto& inner : unsafe_block->body) {
            analyzeStatement(inner.get());
        }
        exitScope();
    }''')

edit(SA, "replace",
     '''void SemanticAnalyzer::visit(UnsafeBlock& node) {
    unsafe_depth++;
    enterScope();
    for (auto& stmt : node.body) { stmt->accept(*this); }
    exitScope();
    unsafe_depth--;
}''',
     '''void SemanticAnalyzer::visit(UnsafeBlock& node) {
    enterScope();
    for (auto& stmt : node.body) { stmt->accept(*this); }
    exitScope();
}''')

edit("PyCpp/include/semantic_analyzer.h", "replace",
     "    int unsafe_depth = 0; // > 0 while inside an 'unsafe:' block\n",
     "")

# ---------------------------------------------------------- syntax highlighter
# Find the tmLanguage.json anywhere under Syntax_highlighter
highlighter_path = None
for p in (ROOT / "Syntax_highlighter").rglob("*.tmLanguage.json"):
    highlighter_path = p.relative_to(ROOT).as_posix()
    break

if highlighter_path:
    try:
        text = (ROOT / highlighter_path).read_text(encoding="utf-8")
        if "|unsafe)" in text:
            edit(highlighter_path, "replace",
                 "(if|elif|else|for|while|break|continue|return|assert|use|unsafe)",
                 "(if|elif|else|for|while|break|continue|return|assert|use)")
        else:
            print(f"[SKIP] {highlighter_path}: 'unsafe' not in keyword list")
    except Exception as e:
        print(f"[WARN] Could not read {highlighter_path}: {e}")
else:
    print("[SKIP] No tmLanguage.json found under Syntax_highlighter")

# ---------------------------------------------------------- README
RD = "README.md"

try:
    readme_text = (ROOT / RD).read_text(encoding="utf-8")
    # Find the paragraph that mentions "unsafe:" and replace the first 3 lines of it
    # (the paragraph, the code block opening, and the unsafe: line) plus the closing text.
    import re
    # Match a paragraph that contains "only allowed inside `unsafe:`" up to the next blank line after the code block
    pattern = re.compile(
        r'(Reading or writing through a pointer.*?Dereferencing outside `unsafe:` is a compile error\.)',
        re.DOTALL
    )
    match = pattern.search(readme_text)
    if match:
        edit(RD, "replace", match.group(1),
             "Pointers give you direct memory access. Taking an address (`&x`), dereferencing (`*ptr`), and assigning through a pointer (`*ptr = 100`) all work anywhere in the language.")
    else:
        print("[SKIP] README: no 'only allowed inside `unsafe:`' paragraph found")
except FileNotFoundError:
    print("[SKIP] README.md not found")

# Try to remove the unsafe feature bullet
try:
    readme_text = (ROOT / RD).read_text(encoding="utf-8")
    if "- `unsafe:` — enable explicitly unsafe operations.\n" in readme_text:
        edit(RD, "replace",
             "- `unsafe:` — enable explicitly unsafe operations.\n",
             "")
except FileNotFoundError:
    pass


# ======================================================================= engine
def apply_edit(text, mode, anchor, new, end):
    if mode == "append":
        if new.strip() in text:
            return text, "skip"
        if not text.endswith("\n"):
            text += "\n"
        return text + new, "ok"

    if new.strip() and new.strip() in text and mode in ("before", "after", "replace"):
        return text, "skip"

    n = text.count(anchor)
    if n != 1:
        raise ValueError(f"anchor found {n} times (need exactly 1):\n---\n{anchor[:200]}---")
    i = text.index(anchor)

    if mode == "before":
        return text[:i] + new + text[i:], "ok"
    if mode == "after":
        j = i + len(anchor)
        return text[:j] + new + text[j:], "ok"
    if mode == "replace":
        return text[:i] + new + text[i + len(anchor):], "ok"
    raise ValueError("unknown mode " + mode)


def main():
    by_file = {}
    for e in EDITS:
        by_file.setdefault(e[0], []).append(e)

    results = {}
    failed = False
    for path, edits in by_file.items():
        p = ROOT / path
        if not p.exists():
            print(f"[MISSING] {path}")
            failed = True
            continue
        raw = p.read_bytes().decode("utf-8")
        crlf = "\r\n" in raw
        text = raw.replace("\r\n", "\n")
        for (_, mode, anchor, new, end) in edits:
            try:
                text, status = apply_edit(text, mode, anchor, new, end)
            except ValueError as ex:
                print(f"[FAIL] {path}: {ex}")
                failed = True
                break
            print(f"[{status.upper():4}] {path}: {mode}")
        else:
            results[p] = text.replace("\n", "\r\n") if crlf else text

    if failed:
        print("\nNothing was written. Fix the reported anchor(s) and re-run.")
        sys.exit(1)

    if CHECK:
        print("\nDry run OK: every edit applies cleanly.")
        return
    for p, text in results.items():
        p.write_bytes(text.encode("utf-8"))
    print(f"\nPatched {len(results)} files.")


if __name__ == "__main__":
    main()