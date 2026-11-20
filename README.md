# **PyCPP — Pythonic Syntax, Native C++ Power**

**Write like Python. Compile like C++. Run at native speed.**

PyCPP is a statically typed, indentation-based programming language that compiles directly to modern C++20 or later.

It combines Python's simplicity and readability with C++'s performance, control, and flexibility, without requiring a runtime or garbage collector.

The goal is simple: **Everything C++ can do, PyCPP should be able to do — with cleaner, more Pythonic syntax.**

# **1. Core Features**

- Python-inspired syntax with C++ semantics.
- Fully static type system.
- Direct compilation to native C++.
- No virtual machine or runtime overhead.
- Automatic standard library header detection.
- Native STL integration.
- Headerless development — no `.h` or `.hpp` files.
- Full C/C++ interoperability.
- Manual memory management.
- Optional unsafe flag.
- Templates and compile-time evaluation.
- Python-like imports and modules.
- Flexible function argument passing.
- Native classes, structs, and inheritance.

# **2. Syntax**

PyCPP uses indentation to define blocks instead of braces `{}` and semicolons `;`.

It aims to feel familiar to Python developers while maintaining predictable C++ semantics.

Supported syntax:

- `def` — functions.
- `class` — classes.
- `struct` — data structures.
- `if`, `elif`, `else` — conditional statements.
- `for`, `while` — loops.
- `break`, `continue`, `return` — flow control.
- `assert` — runtime check with an optional message: `assert x > 0, "x must be positive"`. Compiles to C++ `assert` (disabled by `NDEBUG`).
- Conditional expressions: `label: str = "odd" if n % 2 == 1 else "even"`.
- Membership tests: `x in items` / `x not in items` (list, dict and set membership, substring test for `str`).
- Number literals: hex (`0xFF`), binary (`0b101`) and exponent floats (`1.5e3`). `bin()`, `oct()` and `hex()` return Python-style strings such as `"0b101"`.
- Python arithmetic: `/` is true division (`7 / 2` is `3.5`), `//` floors (`-7 // 2` is `-4`), `%` takes the sign of the divisor, and `**` on integers stays an integer. Dividing by zero stops the program with `ZeroDivisionError`.
- `print()` writes Python-style text: `True` / `False`, floats such as `3.0` and `0.1`, and `[1, 2]` / `{'a': 1}` for containers.
- `list()`, `dict()` and `set()` create empty collections, typed by the declaration: `d: dict[str, int] = dict()`.
- `pycpp` exits with the exit code of the program it ran.
- Exceptions: `try` / `except` / `else` / `finally` and `raise`.
  ```
  def main() -> int:
      try:
          n: int = int("abc")
      except (ValueError, TypeError) as e:
          print(e)
      finally:
          print("done")
      return 0
  ```
  Built-in exceptions: `Exception`, `ArithmeticError`, `ZeroDivisionError`, `OverflowError`, `LookupError`, `IndexError`, `KeyError`, `ValueError`, `TypeError`, `RuntimeError`. Define your own with `class MyError(Exception):`; a leading `str` parameter of its `__init__` becomes its message. Errors such as `int("abc")` and `1 / 0` raise catchable exceptions; uncaught, they print `ValueError: message` and exit with code 1. A variable declared inside a `try` body is not visible after it, so declare it before the `try`.

Example:

```
def main() -> int:
    age: int = 18
    if age >= 18:
        print("Adult")
    else:
        print("Minor")
    return 0
```

The compiler automatically handles C++ syntax generation.

# **3. Type System**

PyCPP is fully statically typed.

Every variable, function parameter, and return type must have an explicitly declared type.

Supported types:

| PyCPP    | C++ equivalent |
| -------- | -------------- |
| `int`    | `int`          |
| `i8`     | `int8_t`       |
| `i16`    | `int16_t`      |
| `i32`    | `int32_t`      |
| `i64`    | `int64_t`      |
| `float`  | `float`        |
| `double` | `double`       |
| `f32`    | `float`        |
| `f64`    | `double`       |
| `bool`   | `bool`         |
| `str`    | `std::string`  |

Additional types:

- `list[T]` — `std::vector<T>`.
- `dict[K, V]` — `std::map<K, V>`.
- `*T` — pointer to T.
- `&T` — reference to T.

Type safety is enforced at compile time. Initialising a variable, assigning, or returning a value of an incompatible type is a **compile error** and stops the build. Arithmetic types convert implicitly, exactly as in C++. Suspicious-but-legal code (for example a mismatched operator) produces a warning and does not stop the build.

Type inference may be introduced in a future version.

# **4. Function Argument System**

PyCPP provides Python-like function calls with the ability to access lower-level C++ argument semantics.

The objective is to make ordinary function calls simple while allowing developers to control exactly how objects are passed.

### **4.1 Default passing**

```
def print_player(player: Player) -> void:
    print(player.name)

def main() -> int:
    player: Player = Player("Alex", 100)
    print_player(player)
    return 0
```

Developers don't need to manually specify pointers or references for every function call.

The declared parameter type determines the passing semantics.

### **4.2 Reference passing**

Use `&T` when a function needs to modify an existing object.

```
def damage_player(player: &Player) -> void:
    player.health -= 10

def main() -> int:
    player: Player = Player("Alex", 100)
    damage_player(player)
    return 0
```

The object is passed by reference rather than copied.

### **4.3 Immutable references**

Use `const &T` when a function needs to inspect an object without modifying it.

```
def inspect_player(player: const &Player) -> void:
    print(player.name)
    print(player.health)
```

This is particularly useful for large objects.

### **4.4 Pointer passing**

Use `*T` for explicit pointer access.

```
def update_player(player: *Player) -> void:
    player.health -= 10
```

Pointers allow developers to perform lower-level operations when needed.

### **4.5 Pointer-to-pointer**

PyCPP also supports multiple pointer levels.

```
def change_pointer(player: **Player) -> void:
    ...
```

### **4.6 Generic functions**

PyCPP supports C++ templates through Pythonic generic syntax.

```
def print_value[T](value: T) -> void:
    print(value)

def main() -> int:
    print_value(100)
    print_value("Hello")
    print_value(3.14)
    return 0
```

Each call is type-checked at compile time.

### **Argument syntax summary**

| Syntax                         | Meaning                              |
| ------------------------------ | ------------------------------------ |
| `value: T`                     | Value parameter                      |
| `value: &T`                    | Mutable reference                    |
| `value: const &T`              | Immutable reference                  |
| `value: *T`                    | Pointer                              |
| `value: **T`                   | Pointer to pointer                   |
| `value: T` in generic function | Template parameter when T is generic |

**Design principle:** Python-like simplicity by default, C++-level control when requested.

# **5. Memory Management**

PyCPP provides manual memory management without a garbage collector.

Memory can be allocated and freed explicitly.

```
def main() -> int:
    player: *Player = mem.alloc[Player]()
    player.health = 100
    mem.free(player)
    return 0
```

Pointers give you direct memory access. Taking an address (`&x`), dereferencing (`*ptr`), and assigning through a pointer (`*ptr = 100`) all work anywhere in the language.

Features:

- `mem.alloc()` — allocate memory.
- `mem.free()` — release memory.
- Pointer arithmetic.
- Direct memory manipulation.

Future enhancements:

- RAII wrappers.
- Ownership tracking.
- Move semantics.
- Safer automatic cleanup.

# **6. Control Flow**

PyCPP supports standard control flow with Python-like syntax.

```
def main() -> int:
    for i in range(10):
        if i == 5:
            continue
        print(i)
    return 0
```

Supported:

- `if`
- `elif`
- `else`
- `for`
- `while`
- `break`
- `continue`
- `return`

Future enhancements:

- `super()` for custom exception classes.
- Additional iteration utilities.

# **7. Classes and Object-Oriented Programming**

PyCPP supports classes, constructors, instance variables, and inheritance.

No manually managed header files are required.

```
class Player:
    def __init__(self, name: str, health: int):
        self.name: str = name
        self.health: int = health

    def damage(self, amount: int) -> void:
        self.health -= amount

def main() -> int:
    player: Player = Player("Alex", 100)

    player.damage(20)

    print(player.health)

    return 0
```

Features:

- Constructors through `__init__`.
- Typed instance variables.
- Methods.
- Inheritance.
- Native C++ class generation.
- Automatic header management.

### **7.1 High-level vs low-level: what the compiler generates**

The same program in three forms: high-level PyCPP (Python-like defaults), low-level PyCPP (explicit references, pointers and manual memory), and the C++20 the compiler generates.

**High level — PyCPP (`main.pycpp`)**

```python
class Player:
    def __init__(self, name: str, health: int):
        self.name: str = name
        self.health: int = health

    def damage(self, amount: int) -> void:
        self.health -= amount

def main() -> int:
    player: Player = Player("Alex", 100)

    player.damage(20)

    print(player.health)

    return 0
```

**Generated C++20 from the high-level version (`main.cpp`)**

```cpp
#include <iostream>   // added automatically: print()
#include <string>     // added automatically: str
#include <utility>    // added automatically: std::move

class Player {
public:
    std::string name;
    int health;

    Player(std::string name, int health)
        : name(std::move(name)), health(health) {}

    void damage(int amount) {
        this->health -= amount;
    }
};

int main() {
    Player player = Player("Alex", 100);

    player.damage(20);

    std::cout << player.health << '\n';

    return 0;
}
```

Output:

```
80
```

**How each part translates**

| PyCPP                                   | Generated C++                                         |
| --------------------------------------- | ----------------------------------------------------- |
| `class Player:`                         | `class Player { public: ... };`                       |
| `self.name: str = name` in `__init__`   | Member `std::string name;` + constructor initializer  |
| `def __init__(self, name: str, ...)`    | Constructor `Player(std::string name, ...)`           |
| `def damage(self, amount: int) -> void` | Member function `void damage(int amount)`             |
| `self.health`                           | `this->health`                                        |
| `def main() -> int:`                    | `int main()`                                          |
| `player: Player = Player("Alex", 100)`  | `Player player = Player("Alex", 100);`                |
| `print(player.health)`                  | `std::cout << player.health << '\n';`                 |
| Indentation                             | Braces `{}` and semicolons `;`                        |
| (nothing)                               | `#include` lines, detected by the Smart Include System |

Notes:

- Typed `self.x: T` assignments in `__init__` become class members, and are initialised in the constructor's initializer list rather than assigned in its body.
- A `str` parameter taken by value is moved into the member (`std::move`), so constructing a `Player` costs no extra string copy.
- Class members are `public` by default, matching Python's attribute access.
- `print()` writes `'\n'` rather than `std::endl`, so it doesn't force a flush on every call.

**Low level — PyCPP (`main_lowlevel.pycpp`)**

The same behaviour, written with the explicit argument and memory features from sections 4 and 5. It is still PyCPP source, not C++.

```python
class Player:
    def __init__(self, name: const &str, health: int):
        self.name: str = name
        self.health: int = health

    def damage(self, amount: int) -> void:
        self.health -= amount

def damage_player(player: *Player, amount: int) -> void:
    player.damage(amount)

def main() -> int:
    player: *Player = mem.alloc[Player]("Alex", 100)

    damage_player(player, 20)
    print(player.health)

    mem.free(player)

    return 0
```

**Generated C++20 from the low-level version**

```cpp
#include <iostream>
#include <string>

class Player {
public:
    std::string name;
    int health;

    Player(const std::string& name, int health)
        : name(name), health(health) {}

    void damage(int amount) {
        this->health -= amount;
    }
};

void damage_player(Player* player, int amount) {
    player->damage(amount);
}

int main() {
    Player* player = new Player("Alex", 100);

    damage_player(player, 20);

    std::cout << player->health << '\n';

    delete player;

    return 0;
}
```

| High level | Low level | C++ (low level) |
|---|---|---|
| `name: str` (passed by value) | `name: const &str` (immutable reference) | `const std::string& name` |
| `player: Player = Player("Alex", 100)` | `player: *Player = mem.alloc[Player](...)` | `Player* player = new Player(...)` |
| `player.damage(20)` | `damage_player(player, 20)` taking `*Player` | `player->damage(20)` |
| `player.health` (safe access) | `player.health` (pointer access) | `player->health` |
| Destroyed automatically at end of scope | `mem.free(player)` called explicitly | `delete player;` |

Notes:

- The spec only shows `mem.alloc[T]()` with no arguments. Passing constructor arguments (`mem.alloc[Player]("Alex", 100)`) is an assumption made here so the example stays equivalent to the high-level one; the spec would need to define it.
- Both versions print `80` and behave identically. The low-level one leaves lifetime and access safety to the programmer: forgetting `mem.free` leaks the object, and using `player` after it would be undefined behaviour.
- The `const &str` constructor parameter copies the string into the member, since a reference can't be moved from the way a by-value parameter can.

# **8. Structs**

Structs provide lightweight data-only containers.

```
struct Vector2:
    x: float
    y: float

def main() -> int:
    position: Vector2 = Vector2(10.0, 20.0)
    print(position.x)
    return 0
```

Structs compile to native C++ structs.

Features:

- Public members.
- Typed fields.
- Optional default values.
- No methods or inheritance.

# **9. Collections**

PyCPP integrates directly with the C++ STL.

| PyCPP       | C++              |
| ----------- | ---------------- |
| `list[T]`   | `std::vector<T>` |
| `dict[K,V]` | `std::map<K,V>`  |
| `str`       | `std::string`    |

Example:

```
def main() -> int:
    numbers: list[int] = [1, 2, 3, 4, 5]
    for number in numbers:
        print(number)
    return 0
```

Dictionary iteration:

```
players: dict[str, int] = {
    "Alex": 100,
    "Sam": 80
}

for name, health in players.items():
    print(name, health)
```

The compiler automatically detects required standard library headers.

# **10. Templates**

PyCPP supports native C++ templates through Pythonic syntax.

Generic functions:

```
def maximum[T](a: T, b: T) -> T:
    if a > b:
        return a
    return b
```

Generic classes:

```
class Container[T]:
    def __init__(self, value: T):
        self.value: T = value
```

Templates compile into native C++ templates.

This allows compatibility with the STL and user-defined types.

# **11. Constexpr**

PyCPP supports compile-time constants through `constexpr`.

```
constexpr MAX_PLAYERS: int = 100
constexpr PI: double = 3.141592653589793
```

These values are evaluated at compile time where possible.

# **12. Imports and Modules**

PyCPP uses Python-style imports for PyCPP source files.

```
from player import Player
import game_utils
```

There is no requirement to manually create or include header files.

The compiler manages dependencies and generates the necessary C++ includes.

Import handling supports module aliases, dotted/package imports, `from ... import ...`,
relative imports within packages, and circular-import detection. Each imported module's
generated declarations are placed in a dedicated C++ namespace so separate modules can
safely define the same function or class name. Cyclic `from` imports are guarded while
headers are being generated; see `examples/imports` for the package, alias, duplicate
module-basename, and cycle examples.

# **13. C/C++ Interoperability**

PyCPP is designed to work alongside existing C and C++ libraries.

Developers can directly include external headers when necessary.

```
use <iostream>
use <cmath>
```

This allows access to existing C++ functionality.

The objective is to avoid reinventing functionality that C++ already provides.

# **14. Smart Include System**

PyCPP automatically detects which standard libraries are needed.

For example:

```
numbers: list[int] = [1, 2, 3]
```

Automatically generates the required:

```
#include <vector>
```

Developers don't need to manually manage standard library headers.

Only necessary headers should be included.

# **15. Compilation**

PyCPP compiles directly to modern C++.

**Compilation pipeline**

**PyCPP source**

main.pycpp

**PyCPP compiler**

Parsing · Type checking · Code generation

**Generated C++**

Modern C++ source

**Native executable**

Compiled using GCC, Clang or another supported compiler

PyCPP introduces no mandatory virtual machine or garbage collector.

Performance should be comparable to equivalent C++ code when the generated code and compiler optimizations are equivalent.

### **15.1 Choosing the C++ compiler**

The generated C++ can be built with GCC or MSVC. Select one with `--compiler`:

```
pycpp main.pycpp --compiler gcc      # g++
pycpp main.pycpp --compiler msvc     # Visual Studio (cl.exe), Windows only
pycpp main.pycpp                     # auto: msvc on Windows, gcc elsewhere
```

| Value          | Compiler          | Notes                                                        |
| -------------- | ----------------- | ------------------------------------------------------------ |
| `gcc` / `g++`  | GCC (MinGW on Windows) | `g++` must be on `PATH`.                                |
| `msvc` / `cl`  | Visual Studio 2022 | Uses `cl.exe` from `PATH`, otherwise loads `vcvars64.bat`. |
| `auto`         | Platform default  | `msvc` on Windows, `gcc` on Linux and macOS.                 |

`--compiler=gcc` is accepted as well. If the chosen compiler is not available, PyCPP stops with a clear message instead of falling back to a different one.

To build the PyCPP compiler itself with either toolchain, use the CMake presets: `linux-gcc-release`, `windows-gcc-release` (MinGW) or `x64-release` (MSVC).

### **15.2 Output and diagnostics**

By default PyCPP prints only what matters: the program's own output, warnings, and errors. Errors go to stderr, carry a source location, and make the command exit with status 1.

```
$ pycpp main.pycpp
80

$ pycpp bad.pycpp
error: line 3:5: syntax error: Expected expression, got: 'return'
```

| Option          | Effect                                                                  |
| --------------- | ----------------------------------------------------------------------- |
| `-v`, `--verbose` | Show the compiler's progress, the exact compile command, and the lexer/parser/analyzer trace. |
| `--keep-temp`   | Keep the generated `.h`/`.cpp` files and the executable in `temp/`.     |
| `--no-compile`  | Generate the `.h` and `.cpp` files only.                                |

Syntax errors and type errors stop the build. The parser never skips a statement it cannot understand.

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
- `match` / `case` pattern selection, including multiple literal alternatives
  (`case 2 | 3`) and the wildcard fallback (`case _`).
- `defer` schedules a statement for scope exit, including early returns; the
  low-level example uses it to release a `mem.alloc` allocation.
- `input(prompt)` reads a typed value from standard input (the destination
  variable's declared type determines conversion).
- `use <header>` includes an external C/C++ header. External libraries can be
  passed to the generated-code linker with `--linker-flags`.

### Example projects

| Folder | What it demonstrates |
| --- | --- |
| `simple` | Basic output, imports, input, arithmetic, built-ins, collections, conditionals, membership tests, literals, and exceptions. |
| `oop` | Classes, constructors, methods, and object-oriented code. |
| `nesting` | Nested control flow and imports across modules. |
| `module_regression` | Regression checks for duplicate exported names across imported modules through distinct module-qualified imports. |
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

# **16. Design Philosophy**

PyCPP follows five core principles:

1. **Readable:** Code should look and feel like Python.
2. **Powerful:** Developers should have access to C++ capabilities.
3. **Predictable:** Static typing and explicit semantics should make behavior understandable.
4. **Native:** Generated code should compile directly to native machine code.
5. **Flexible:** Beginners should be able to write simple code, while advanced developers retain low-level control.

# **The PyCPP Vision**

# **PyCPP**

## Pythonic Syntax. Native C++ Power.

Write less boilerplate. Keep complete control. Get native performance.

**The simplicity of Python. The power of C++.**

*Note: PyCPP is an AI-designed language project. Features described as supported represent the proposed language specification; future enhancements are planned rather than necessarily implemented.*