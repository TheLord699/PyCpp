#!/usr/bin/env python3
"""
fix.py -- fixes the member access bug on imported module names.
Run from the repo root:
    python fix.py            # apply
    python fix.py --check    # dry run
"""
import sys
from pathlib import Path

ROOT = Path(".")
CHECK = "--check" in sys.argv
EDITS = []


def edit(path, mode, anchor, new, end=None):
    EDITS.append((path, mode, anchor, new, end))


CG = "PyCpp/src/code_generator.cpp"

# Fix: imported module check should not fire if the identifier is a local variable/parameter.
edit(CG, "replace",
     '''        // Check if this is module access (like greetings.hello)
        if (auto ident = dynamic_cast<Identifier*>(member_access->object.get())) {
            // Only identifiers that name an imported module are module accesses;
            // anything else (e.g. a local variable 'player') is an object access
            if (ident->name != "self" && ident->name != "this" &&
                imported_modules.count(ident->name) > 0) {
                // This is likely a module access like greetings.hello
                // For now, generate as direct function call since we include the header
                // In a more advanced implementation, we might use namespaces
                output << member_access->member;
                return; // Early return to avoid the dot operator
            }
        }''',
     '''        // Check if this is module access (like greetings.hello)
        if (auto ident = dynamic_cast<Identifier*>(member_access->object.get())) {
            // Only identifiers that name an imported module are module accesses;
            // anything else (e.g. a local variable 'player') is an object access
            if (ident->name != "self" && ident->name != "this" &&
                imported_modules.count(ident->name) > 0 &&
                lookupVar(ident->name) == nullptr) {
                // This is likely a module access like greetings.hello
                // For now, generate as direct function call since we include the header
                // In a more advanced implementation, we might use namespaces
                output << member_access->member;
                return; // Early return to avoid the dot operator
            }
        }''')


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