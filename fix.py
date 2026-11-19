#!/usr/bin/env python3
'''Update PyCPP's example runner and README with currently shipped examples.

Run from the repository root:
    python fix.py
The script is idempotent and only edits run_all.ps1 and README.md.
'''
from pathlib import Path

HERE = Path(__file__).resolve().parent
CANDIDATES = [
    HERE,
    HERE / "PyCpp",
    HERE / "PyCpp_patchwork" / "PyCpp",
    HERE.parent / "PyCpp",
]
ROOT = next((candidate for candidate in CANDIDATES
             if (candidate / "README.md").exists()
             and (candidate / "run_all.ps1").exists()), HERE)
RUNNER = ROOT / "run_all.ps1"
README = ROOT / "README.md"

README_MARKER = "## Examples and recently demonstrated syntax"

README_SECTION = r'''
## Examples and recently demonstrated syntax

The `PyCpp/examples` directory contains runnable examples grouped by topic. The
`run_all.ps1` script runs the console examples and, unless `-NoGui` is supplied,
the Raylib demos. Supporting module files are compiled through their importing
entry points rather than run independently.

### Additional syntax and language features

These are demonstrated in the examples:

- `constexpr NAME: T = value` for compile-time constants.
- Conditional expressions: `value = a if condition else b`.
- Membership: `item in collection` and `item not in collection`, including string
  substring checks.
- Numeric literals: hexadecimal (`0xFF`), binary (`0b101`), and exponent
  notation (`1.5e2`); conversion helpers `bin()`, `oct()`, and `hex()`.
- Arithmetic: true division `/`, floor division `//`, modulo `%`, exponentiation
  `**`, and compound floor-division assignment (`//=`).
- Collection helpers demonstrated by examples include `len`, `range`, `sum`,
  `min`, `max`, `sorted`, `randint`, list `append`, indexing, and dictionary
  `items()` iteration.
- Exception handling: `try`, `except`, optional exception binding, multiple
  exception types, `else`, `finally`, `raise`, bare `raise`, and custom
  exception classes derived from `Exception`.
- Pointer operations: address-of, dereference, pointer arithmetic, pointer
  chains, heap allocation through `mem.alloc[T]`, and release through
  `mem.free`.
- Pointer element iteration: range-for over pointer-valued collections retains
  the pointer element type, allowing member access to generate pointer access
  (`->`) instead of value access (`.`).

### Example projects

| Folder | What it demonstrates |
| --- | --- |
| `simple` | Basic output, imports, input, arithmetic, built-ins, collections, conditionals, membership tests, literals, and exceptions. |
| `oop` | Classes, constructors, methods, and object-oriented code. |
| `nesting` | Nested control flow and imports across modules. |
| `low_level` | Allocation, references, pointer arithmetic, pointer chains, `defer`, `match`, and assertions. |
| `raylib` | Window drawing, particles, Snake, Dodge, and a multi-file Space Defender project. |

### Running the examples

From the repository root in PowerShell:

```powershell
.\run_all.ps1
# Skip Raylib graphical examples:
.\run_all.ps1 -NoGui
# Save verbose compiler output for each run:
.\run_all.ps1 -Verbose
```

Raylib examples require Raylib and the linker flags configured in the runner.
'''

def add_runs(text: str) -> str:
    anchor = 'Run "12. arithmetic"     "simple"       "arithmetic.pycpp"'
    additions = [
        'Run "13. pointer chain"  "low_level"    "pointer_chain.pycpp"',
        'Run "14. exceptions"     "simple"       "exceptions.pycpp"',
    ]
    if all(line in text for line in additions):
        return text
    if anchor not in text:
        raise RuntimeError("Could not find arithmetic runner line in run_all.ps1; refusing to guess.")
    missing = [line for line in additions if line not in text]
    return text.replace(anchor, anchor + "\n" + "\n".join(missing), 1)

def renumber_gui(text: str) -> str:
    replacements = {
        '"13. raylib window"': '"15. raylib window"',
        '"14. raylib dodge"': '"16. raylib dodge"',
        '"15. raylib game"': '"17. raylib game"',
    }
    for old, new in replacements.items():
        if old in text and new not in text:
            text = text.replace(old, new, 1)
    return text

def add_raylib_runs(text: str) -> str:
    additions = [
        'Run "18. raylib particles" "raylib" "particles.pycpp" $gui',
        'Run "19. raylib snake"     "raylib" "snake.pycpp"     $gui',
    ]
    if all(line in text for line in additions):
        return text
    anchor = next((line for line in text.splitlines() if 'raylib\\game' in line and 'main.pycpp' in line), None)
    if not anchor:
        raise RuntimeError("Could not find Raylib game runner line; refusing to guess.")
    missing = [line for line in additions if line not in text]
    return text.replace(anchor, anchor + "\n" + "\n".join(missing), 1)

def update_readme(text: str) -> str:
    if README_MARKER in text:
        return text
    future_heading = "# **16. Future Development**"
    if future_heading in text:
        return text.replace(future_heading, README_SECTION.strip() + "\n\n" + future_heading, 1)
    return text.rstrip() + "\n\n" + README_SECTION.strip() + "\n"

def main() -> None:
    if not RUNNER.exists() or not README.exists():
        raise SystemExit("Could not locate run_all.ps1 and README.md. Place fix.py in the project root.")
    runner_text = RUNNER.read_text(encoding="utf-8")
    readme_text = README.read_text(encoding="utf-8")

    runner_text = add_runs(runner_text)
    runner_text = renumber_gui(runner_text)
    runner_text = add_raylib_runs(runner_text)
    readme_text = update_readme(readme_text)

    RUNNER.write_text(runner_text, encoding="utf-8")
    README.write_text(readme_text, encoding="utf-8")
    print("Updated run_all.ps1 and README.md.")

if __name__ == "__main__":
    main()
