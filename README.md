# **PyCPP — Pythonic Syntax, Native C++ Power**

[![OctoCounts](https://api.octocounts.com/badge/TheLord699/PyCpp/branch/master)](https://octocounts.com/github/TheLord699/PyCpp/tree/master)

**Write like Python. Compile like C++. Run at native speed.**

PyCPP is a statically typed, indentation-based programming language that compiles directly to modern C++20 or later.

It combines Python's simplicity and readability with C++'s performance, control, and flexibility, without requiring a runtime or garbage collector.

The goal is simple: **Everything C++ can do, PyCPP should be able to do — with cleaner, more Pythonic syntax.**

# Quick Start

Get started with PyCPP in a few minutes.

PyCPP translates `.pycpp` source files into modern C++ and then compiles them into native executables using GCC, Clang or MSVC.

## 1. Prerequisites

Before building PyCPP, make sure you have the following installed:

- **Git** — to clone the repository.
- **CMake** — to configure and build the project.
- **C++ compiler** — GCC (g++), Clang (clang++), or Microsoft Visual C++ (MSVC).

Make sure your chosen C++ compiler and CMake are accessible through your system PATH.

For Windows GCC builds, you need a working MinGW-w64 installation.

On macOS, install Apple Command Line Tools with `xcode-select --install` to get Clang and the macOS SDK. Install CMake separately (for example, `brew install cmake`).

## 2. Download PyCPP

Clone the repository and enter its directory:

```bash
git clone https://github.com/TheLord699/PyCpp.git
cd PyCpp
```

## 3. Build the PyCPP compiler

Choose the build configuration that matches your platform and compiler.

**Windows (GCC / MinGW):**

```bash
cmake --preset windows-gcc-release
cmake --build --preset windows-gcc-release
```

**Windows (MSVC):**

```bash
cmake --preset x64-release
cmake --build --preset x64-release
```

**Linux (GCC):**

```bash
cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release
```

**macOS (Clang):**

```bash
cmake --preset macos-release
cmake --build --preset macos-release
```

These commands configure and build the PyCPP compiler itself in Release mode.

After building, locate the generated `pycpp` executable and add its directory to your system PATH if you want to run it from any directory.

Alternatively, you can invoke it directly using its full path.

## 4. Write your first PyCPP program

Create a file named `hello.pycpp`:

```python
def main() -> int:
    print("Hello from PyCPP")
    return 0
```

PyCPP uses Python-inspired syntax, but unlike Python, all variables and function signatures are statically typed.

## 5. Compile and run

**Using Clang:**

```bash
pycpp hello.pycpp --compiler clang
```

**Using GCC:**

```bash
pycpp hello.pycpp --compiler gcc
```

**Using MSVC (Windows):**

```bash
pycpp hello.pycpp --compiler msvc
```

The compiler will:

1. Parse your PyCPP source code.
2. Perform type checking and semantic analysis.
3. Generate equivalent modern C++ code.
4. Invoke the selected C++ compiler.
5. Produce and execute the native program.

Expected output:

```text
Hello from PyCPP
```

PyCPP returns the exit code of the program it executes.

## 6. Compiler selection

You can explicitly choose which C++ compiler PyCPP uses:

| Argument          | Compiler                     |
| ----------------- | ---------------------------- |
| `--compiler clang` | Clang / clang++                   |
| `--compiler gcc`  | GCC / g++                    |
| `--compiler msvc` | Microsoft Visual C++         |
| No argument       | Automatic platform selection |

Automatic selection defaults to MSVC on Windows, Clang on macOS, and GCC on Linux.

If the selected compiler cannot be found, PyCPP reports an error rather than silently switching to another compiler.

**That's it!** You have successfully built the PyCPP compiler and compiled your first PyCPP program into a native executable.


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
- f-strings with Python's format specs: `f"{name!r:>10} {price:8.2f} {count:,} {flags:#x} {ratio:.1%} {total=}"`. `{{` and `}}` are literal braces.
- Comprehensions: `[x * x for x in xs if x > 0]`, `{k: len(k) for k in words}`, `{n % 3 for n in nums}`, nested `for` clauses, and generator expressions passed to a call: `sum(x * x for x in xs)`.
- `lambda a, b: a + b` (captures by reference; a lambda that is returned or stored in a member captures by copy).
- Python methods of `str`, `list`, `dict` and `set` (`s.upper()`, `line.split(",")`, `"-".join(words)`, `xs.pop()`, `d.get(k, 0)`, ...), see [Collections](#9-collections).
- `super()`: `super().__init__(...)` (as the first statement of `__init__`) and `super().method(...)`, see [Classes](#7-classes-and-object-oriented-programming).
- `with`, `yield`, `async`/`await`, `global` and `nonlocal` are not supported; the error message suggests the PyCPP alternative (`defer` instead of `with`, a list instead of a generator, ...).
- `pycpp` exits with the exit code of the program it ran.
- Exceptions: `try` / `except` / `else` / `finally` and `raise`.
  ```python
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

```python
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

Every function parameter and return type has an explicitly declared type. A local variable can be declared with a type (`count: int = 0`) or, as in Python, just assigned (`count = 0`), in which case it gets the type of its value (see [Type inference](#type-inference)).

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
- `set[T]` — `std::set<T>`.
- `*T` — pointer to T.
- `&T` — reference to T.

Type safety is enforced at compile time. Initialising a variable, assigning, or returning a value of an incompatible type is a **compile error** and stops the build. Arithmetic types convert implicitly, exactly as in C++. Suspicious-but-legal code (for example a mismatched operator) produces a warning and does not stop the build.

Integer literals are 64-bit: a literal that fits in `int` has type `int`, a larger one (up to `9223372036854775807`) has type `i64`. Storing a literal that does not fit (`x: int = 3000000000`) is an error.

### Type inference

Inside a function, assigning to a name that is not declared yet declares it, with the type of the value:

```python
def main() -> int:
    count = 0                 # int
    ratio = 7 / 2             # double (true division)
    name = "Ann"              # str
    scores = [90, 85]         # list[int]
    ages = {"Ann": 31}        # dict[str, int]
    player = Player("Ann")    # Player
    count = "many"            # error[E0104]: mismatched types: expected int, found str
    return 0
```

The variable is visible in the block it was first assigned in, like a declared one; a variable first assigned inside an `if` is not visible after it (the error says where it was declared). Values without an element type, such as `[]`, `{}`, `list()` or `None`, cannot be inferred: declare those (`items: list[int] = []`).

### What is checked

- Calls of functions, methods and constructors (`Player("Ann")` against `Player.__init__`): the number of arguments and their types, including calls into imported modules (modules are analysed after the modules they import).
- Calls of methods that do not exist on a class, or on `str`/`list`/`dict`/`set` (`name.uper()` suggests `upper`).
- The return type of methods, so `label: str = account.deposit(5)` is an error when `deposit` returns `int`.
- That every path through a function with a return type returns a value (`W0003`). Paths that end in `raise`, `exit()`, or a `while True:` loop without `break` need no return.
- True division where an integer is expected (`W0008`): `/` always produces a float, so `half: int = n / 2`, `return n / 2` from an `int` function, passing `n / 2` to an `int` parameter, `xs[n / 2]` and `x /= 2` on an integer warn. Use `//`.

# **4. Function Argument System**

PyCPP provides Python-like function calls with the ability to access lower-level C++ argument semantics.

The objective is to make ordinary function calls simple while allowing developers to control exactly how objects are passed.

### **4.1 Default passing**

```python
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

```python
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

```python
def inspect_player(player: const &Player) -> void:
    print(player.name)
    print(player.health)
```

This is particularly useful for large objects.

### **4.4 Pointer passing**

Use `*T` for explicit pointer access.

```python
def update_player(player: *Player) -> void:
    player.health -= 10
```

Pointers allow developers to perform lower-level operations when needed.

### **4.5 Pointer-to-pointer**

PyCPP also supports multiple pointer levels.

```python
def change_pointer(player: **Player) -> void:
    ...
```

### **4.6 Generic functions**

PyCPP supports C++ templates through Pythonic generic syntax.

```python
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

```python
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

```python
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

- Additional iteration utilities.

# **7. Classes and Object-Oriented Programming**

PyCPP supports classes, constructors, instance variables, and inheritance.

No manually managed header files are required.

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

Features:

- Constructors through `__init__`.
- Typed instance variables.
- Methods.
- Inheritance.
- Native C++ class generation.
- Automatic header management.

### Calling the base class: `super()`

`super().__init__(...)` constructs the base class. It must be the first statement of `__init__` (C++ builds the base before the derived class), and its arguments cannot use `self`. `super().method(...)` calls the base class's version of a method.

```python
class Entity:
    def __init__(self, pos: Vec2, size: float):
        self.pos: Vec2 = pos
        self.size: float = size

    def describe(self) -> str:
        return "entity"

class Player(Entity):
    def __init__(self, start: Vec2):
        super().__init__(start, 40.0)        # -> Player(Vec2 start) : Entity(start, 40.0)
        self.speed: float = 6.0

    def describe(self) -> str:
        return "player, an " + super().describe()   # -> Entity::describe()
```

It works the same for exception classes: `class AppError(Exception)` can call `super().__init__(message)`.

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

```text
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

```python
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

```python
def main() -> int:
    numbers: list[int] = [1, 2, 3, 4, 5]
    for number in numbers:
        print(number)
    return 0
```

Dictionary iteration:

```python
players: dict[str, int] = {
    "Alex": 100,
    "Sam": 80
}

for name, health in players.items():
    print(name, health)
```

Comprehensions build collections in one expression:

```python
evens = [n for n in numbers if n % 2 == 0]
lengths = {name: len(name) for name in names}
```

### Methods of `str`, `list`, `dict` and `set`

These have Python's behaviour (a user class that defines a method with the same name keeps its own):

| Type   | Methods |
| ------ | ------- |
| `str`  | `upper` `lower` `strip` `lstrip` `rstrip` `split` `rsplit` `splitlines` `join` `replace` `find` `rfind` `index` `count` `startswith` `endswith` `isdigit` `isalpha` `isalnum` `isspace` `isupper` `islower` `title` `capitalize` `swapcase` `center` `ljust` `rjust` `zfill` |
| `list` | `append` `extend` `insert` `pop` `remove` `index` `count` `clear` `sort` `reverse` `copy` |
| `dict` | `get` `keys` `values` `items` `pop` `update` `clear` `copy` `setdefault` |
| `set`  | `add` `remove` `discard` `clear` `copy` |

`xs.pop()` on an empty list raises `IndexError`, `xs.remove(x)` and `s.index(sub)` raise `ValueError` when nothing is found, and `d.pop(k)` raises `KeyError` for a missing key, as in Python. `d.get(k)` without a default returns the value type's default (`0`, `""`, ...) where Python returns `None`. The C++ members of these types (`size()`, `push_back()`, ...) remain available.

`{1, 2, 3}` is a set literal (`{}` is an empty dict). Indexing a dict with a missing key raises `KeyError`, as in Python; assigning (`d[k] = v`) inserts the key. A class that defines `__str__` can be printed, converted with `str()` and used in f-strings.

Tuples: `t: tuple[int, str] = (1, "a")`, `return a, b`, `t[0]` (literal index), and unpacking `lo, hi = min_max(xs)` or `a, b = b, a` (new names are declared with the tuple's element types). `print(t)` shows `(1, 'a')`.

The compiler automatically detects required standard library headers.

# **10. Templates**

PyCPP supports native C++ templates through Pythonic syntax.

Generic functions:

```python
def maximum[T](a: T, b: T) -> T:
    if a > b:
        return a
    return b
```

Generic classes:

```python
class Container[T]:
    def __init__(self, value: T):
        self.value: T = value
```

Templates compile into native C++ templates.

This allows compatibility with the STL and user-defined types.

# **11. Constexpr**

PyCPP supports compile-time constants through `constexpr`.

```cpp
constexpr MAX_PLAYERS: int = 100
constexpr PI: double = 3.141592653589793
```

These values are evaluated at compile time where possible.

# **12. Imports and Modules**

PyCPP uses Python-style imports for PyCPP source files.

```python
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

```cpp
use <iostream>
use <cmath>
```

This allows access to existing C++ functionality.

The objective is to avoid reinventing functionality that C++ already provides.

# **14. Smart Include System**

PyCPP automatically detects which standard libraries are needed.

For example:

```python
numbers: list[int] = [1, 2, 3]
```

Automatically generates the required:

```cpp
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

The generated C++ can be built with GCC, Clang or MSVC. Select one with `--compiler`:

```bash
pycpp main.pycpp --compiler gcc      # GCC (g++)
pycpp main.pycpp --compiler clang    # Clang (clang++)
pycpp main.pycpp --compiler msvc     # Visual Studio (cl.exe), Windows only
pycpp main.pycpp                     # auto: msvc on Windows, clang on macOS, gcc on Linux
```

| Value | Compiler | Notes |
|---|---|---|
| `gcc` / `g++` | GCC (MinGW on Windows) | `g++` must be on `PATH`. |
| `clang` / `clang++` | Clang | `clang++` must be on `PATH`. |
| `msvc` / `cl` | Microsoft Visual C++ (Visual Studio 2022) | Uses `cl.exe` from `PATH`, otherwise loads `vcvars64.bat`. |
| `auto` | Platform default | `msvc` on Windows, `clang` on macOS, `gcc` on Linux. |

`--compiler=gcc`, `--compiler=clang` and `--compiler=msvc` are also accepted. If the chosen compiler is not available, PyCPP stops with a clear message instead of falling back to a different one.

To build the PyCPP compiler itself, use the CMake presets: `linux-gcc-release` (GCC), `windows-gcc-release` (MinGW) or `x64-release` (MSVC). The `macos-release` preset builds with Apple Clang.

### **15.2 Output and diagnostics**

By default PyCPP prints only what matters: the program's own output, warnings, and errors. Errors go to stderr, carry a source location, and make the command exit with status 1.

```console
$ pycpp main.pycpp
80

$ pycpp bad.pycpp
error[E0101]: cannot find 'scor' in this scope
 --> bad.pycpp:3:5
  |
2 |     score: int = 0
3 |     scor += 1
  |     ^^^^
  |
  = help: a similar name exists, did you mean 'score'?

error: could not compile due to 1 error
```

| Option          | Effect                                                                  |
| --------------- | ----------------------------------------------------------------------- |
| `-v`, `--verbose` | Show the compiler's progress, the exact compile command, and the lexer/parser/analyzer trace. |
| `--keep-temp`   | Keep the generated `.h`/`.cpp` files and the executable in `temp/`.     |
| `--no-compile`  | Generate the `.h` and `.cpp` files only.                                |
| `check`, `--check` | Parse and analyse every module, report problems, and stop: `pycpp check main.pycpp`. No C++ is generated or compiled. |
| `--watch`       | Run (or check) again every time one of the program's source files changes. Stop with Ctrl+C. |
| `--error-format <f>` | `human` (default), `short` (`file:line:col: error[E0101]: message`, one per line) or `json` (one object per line). |
| `--explain <code>` | Describe a diagnostic code: `pycpp --explain E0101`.                 |
| `-j <n>`, `--jobs <n>` | Compile up to n modules at once (default: one per CPU core).     |
| `--no-cache`    | Recompile every module instead of reusing unchanged ones.               |

Syntax errors and type errors stop the build. The parser never skips a statement it cannot understand.

### **15.3 Incremental and parallel builds**

Each module is compiled to its own object file, and object files are kept in `temp/.cache/` next to the entry file. A module is recompiled only when its generated C++, a generated header it includes, or the compile command (compiler, `--build`) changed, so editing one function body of a large program recompiles one module. Changing what a module exports (a class, a function signature) also recompiles the modules that import it. Modules that do need compiling are compiled in parallel (`-j`). With MSVC, all of them are passed to one `cl /MP` call, so the Visual Studio environment is set up only once.

`temp/.cache/` is safe to delete at any time; `--no-cache` ignores it.

### **15.4 Editor integration**

`pycpp check file.pycpp --error-format=short` prints one line per problem in the format most editors understand. `.vscode/tasks.json` contains three tasks built on it: **pycpp: check current file** (the default build task, Ctrl+Shift+B), **pycpp: watch current file** (re-checks on every save), and **pycpp: run current file**. Their problem matcher underlines errors and warnings in the editor and lists them in the Problems panel.

For tools and extensions, `--error-format=json` prints one JSON object per line on stderr:

```json
{"file":"/src/game.pycpp","line":12,"column":9,"end_line":12,"end_column":13,"severity":"error","code":"E0101","name":"unknown-name","label":"","hint":"did you mean 'score'?","notes":[],"message":"cannot find 'scor' in this scope"}
```

`end_column` is exclusive. `"message"` is always the last key.

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

*Note: PyCPP is an AI-designed language project.

## Diagnostics and tests

PyCPP reports problems in a compact, Rust-like format: an error code frame with the source line, a caret under the
offending token, a label, notes and a `help:` line. One run reports as many independent errors as it can, followed by a
summary (`could not compile due to 2 errors; 1 warning emitted`). Set `NO_COLOR=1` to disable colours.

Compile-time checks include: unknown names and types (with "did you mean"), wrong argument counts and types in calls to
your own functions, methods and constructors, unknown methods, statements outside functions, a missing `main`, unused
variables, unreachable code, functions that can end without returning, and floats or true divisions stored in integers.
The generated C++ carries `#line` directives, so if the C++ compiler still rejects something, the error is shown against
the PyCPP line that produced it.

The carets underline the whole construct a diagnostic is about: all of `total / count`, not just its last token; an
unterminated string is underlined to the end of the line. Errors and warnings are printed together, sorted by file, line
and column.

**Diagnostic codes.** Every error and warning has a stable code (`error[E0101]`, `warning[W0001]`), so each one can be
documented, searched for and tested precisely. `pycpp --explain E0101` describes a code; the full list is in
[`PyCpp/docs/error-codes.md`](PyCpp/docs/error-codes.md). Codes `E00xx` are syntax errors, `E01xx` name and type
errors, `E02xx` module and build errors, `E03xx` command-line errors, and `Wxxxx` warnings.

**Silencing a warning.** A `# allow:` comment silences warnings on its line, by name or code; on a comment line of its
own it applies to the next line. Errors cannot be silenced.

```python
scratch: int = compute()       # allow: unused
# allow: division
pixels: int = width / 2
legacy: int = 1                # allow: W0001, W0008
```

Warning names: `unused` (W0001), `unreachable` (W0002), `missing-return` (W0003), `float-to-int` (W0004),
`mismatched-operands` (W0005), `no-member` (W0006), `unknown-exception` (W0007), `division` (W0008), `index-type`
(W0009); `all` silences every warning on the line.

`list[i]` is bounds-checked: negative indices count from the end and an out-of-range index raises `IndexError`.

Regression tests live in `PyCpp/tests/` (`errors`, `warnings`, `crashes`, `regressions`, `diagnostics`, `language`,
`types`, `tooling`, `modules`). Each case declares what it expects in leading comments (`# expect-exit: 1`,
`# expect-stderr: error[E0101]`, `# expect-stderr-sequence: ...` for output that must appear in order, `# runs: 2` to
run a case twice). Run them with:

```bash
python PyCpp/tests/run_tests.py                  # everything
python PyCpp/tests/run_tests.py errors crashes   # some folders
python PyCpp/tests/run_tests.py -k unused -v     # filter by name, show output
```
