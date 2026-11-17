
!!NOTE: THIS ENTIRE PROJECT IS AI!!


# **PyCPP — Pythonic Syntax, Native C++ Power**

> **Write like Python. Compile like C++. Run at native speed.**

---

## Overview

**PyCPP** is a statically typed, indentation-based programming language that compiles directly to **modern C++**.

It combines the **simplicity and readability of Python** with the **performance and control of C++**, without any runtime overhead.

### Core Idea

> If C++ can do it, **PyCPP can too** — but with **cleaner, more Pythonic syntax** and **automatic header and memory management**.

---

## Highlights

- **Pythonic syntax**, **C++ semantics**
- **Static typing** — no runtime overhead
- **Python-style OOP** — classes, `self`, constructors, methods, and inheritance
- **Native STL integration** — `list`, `dict`, `str` backed by `std::vector`, `std::map`, `std::string`
- **Smart Include System** — automatically includes only standard libraries when needed
- **Headerless design** — no `.h` or `.hpp` files
- **Full C/C++ interop** via `use <header>`
- **Manual memory control** with optional unsafe blocks
- **Flexible argument passing** — simple Pythonic calls with explicit C++-style control
- **References, pointers, and ownership control**
- **Generic functions** using native C++ templates
- **Compile-time constants**, **templates**, **structs**
- **Native performance** equal to compiled C++

---

## 1. Syntax

PyCPP uses indentation to define blocks — no braces `{}` or semicolons `;`.

Its syntax closely resembles Python, while enforcing C++-like static typing.

- `def` for functions
- `class` and `struct` for types
- Python-style control flow (`if`, `elif`, `else`, `for`, `while`)
- `break`, `continue`, and `return` all supported
- Automatic `return 0` in `main()` if omitted

Future enhancements include **list comprehensions** and **slicing**.

---

## 2. Typing System

PyCPP is fully **statically typed**, with every variable, function argument, and return type declared explicitly.

### Supported Types

- Primitive types (`int`, `float`, `double`, `bool`, `str`)
- STL-backed types (`list[T]`, `dict[K,V]`)
- Pointer types (`*T`)
- Reference types (`&T`)
- Type aliases:
  - Integers: `i8`, `i16`, `i32`, `i64`
  - Floats: `f32`, `f64`

Type-safe operations are enforced at compile-time.

Optional **type inference** is planned for the future.

---

## 3. Memory Management

PyCPP gives full manual control over memory allocation — no garbage collector.

- Allocate and free memory manually using `mem.alloc()` and `mem.free()`
- Perform pointer arithmetic inside `unsafe:` blocks
- References provide direct access to existing objects
- Explicit pointers allow low-level memory manipulation
- Outside unsafe blocks, strict safety checks are enforced
- No automatic garbage collection
- Future versions will introduce **RAII wrappers** for safer automatic memory cleanup

---

## 4. Control Flow

Supports all common control flow structures with Python-like syntax:

- `if`, `elif`, `else`
- `while`, `for`
- `break`, `continue`

Future versions will add **exception handling** (`try/except`).

---

## 5. Functions

Functions are defined using Python-style syntax with **required return types**.

Default parameters and native C++ type returns are supported.

- Functions cannot be overloaded yet (planned)
- Pythonic function bodies generate standard C++ functions
- Simple function calls use clean, Python-like syntax
- Explicit references and pointers allow greater control over argument passing
- Generic functions support reusable code across multiple statically typed types through templates

### Pythonic Argument Passing

PyCPP allows developers to pass objects naturally, without requiring explicit pointer or reference syntax in every function call.

```python
def print_player(player: Player) -> void:
    print(player.name)

def main() -> int:
    player: Player = Player("Alex", 100)

    print_player(player)
    return 0
```

### Explicit Reference Passing

For functions that need to modify an existing object, developers can explicitly request a mutable reference.

```python
def damage_player(player: &Player) -> void:
    player.health -= 10

def main() -> int:
    player: Player = Player("Alex", 100)

    damage_player(player)
    return 0
```

### Immutable Reference Passing

When a function only needs to read an object, it can accept an immutable reference.

```python
def inspect_player(player: const &Player) -> void:
    print(player.name)
    print(player.health)
```

### Pointer Passing

For lower-level memory operations, PyCPP supports explicit pointer types.

```python
def update_player(player: *Player) -> void:
    unsafe:
        player.health = 50
```

### Argument Passing Semantics

| Parameter syntax | Semantics |
|---|---|
| `value: T` | Pass by value; creates an independent copy |
| `value: &T` | Mutable reference to the original object |
| `value: const &T` | Immutable reference to the original object |
| `value: *T` | Pointer to an object |
| `value: **T` | Pointer to a pointer |

**Core rules:**

1. Ordinary `T` parameters always have value semantics.
2. Mutable references must be explicitly declared using `&T`.
3. Immutable references use `const &T`.
4. Pointers provide explicit low-level memory access.
5. The compiler must never silently change value semantics into reference semantics.
6. The compiler may optimize generated C++ code as long as observable PyCPP semantics remain unchanged.

**Design principle: Explicit semantics, optimized execution.**

---

## 6. Collections

PyCPP provides Pythonic collections backed by the C++ STL.

| PyCPP | C++ Equivalent |
|---|---|
| `list[T]` | `std::vector<T>` |
| `dict[K,V]` | `std::map<K,V>` |
| `str` | `std::string` |

### Supported Features

- Direct iteration (`for x in nums:`)
- Key/value iteration (`for k, v in items.items():`)
- Automatic header detection and inclusion

Future enhancements include **slicing** and **comprehensions**.

---

## 7. Classes (Pythonic OOP)

PyCPP supports Python-inspired object-oriented programming while compiling classes directly into native C++ classes.

Classes use Python-style syntax, including:

- `__init__` for constructors
- `self` for accessing the current instance
- Instance attributes with explicit static types
- Instance methods
- Class inheritance
- Method calls using dot notation
- Direct access to instance members according to their access rules

### Basic Class Example

```python
class Player:
    def __init__(self, name: str, health: int) -> void:
        self.name: str = name
        self.health: int = health

    def take_damage(self, amount: int) -> void:
        self.health -= amount

    def heal(self, amount: int) -> void:
        self.health += amount

    def display(self) -> void:
        print(self.name)
        print(self.health)
```

### Creating Objects

```python
def main() -> int:
    player: Player = Player("Alex", 100)

    player.display()

    player.take_damage(25)
    player.display()

    player.heal(10)
    player.display()

    return 0
```

### Understanding `self`

`self` represents the current instance of a class.

It allows methods to access and modify the object's own attributes.

```python
class Counter:
    def __init__(self) -> void:
        self.value: int = 0

    def increment(self) -> void:
        self.value += 1

    def reset(self) -> void:
        self.value = 0

    def get_value(self) -> int:
        return self.value
```

Using `self` avoids needing to explicitly pass the current object into every method call.

The compiler automatically translates `self` into the appropriate C++ instance access.

### Class Inheritance

PyCPP supports Python-style inheritance.

```python
class Animal:
    def __init__(self, name: str) -> void:
        self.name: str = name

    def speak(self) -> void:
        print("Animal sound")


class Dog(Animal):
    def __init__(self, name: str, breed: str) -> void:
        super().__init__(name)
        self.breed: str = breed

    def bark(self) -> void:
        print("Woof!")
```

### Using Inherited Classes

```python
def main() -> int:
    dog: Dog = Dog("Buddy", "Labrador")

    print(dog.name)
    print(dog.breed)

    dog.speak()
    dog.bark()

    return 0
```

### OOP Design Principles

- `self` is automatically available inside instance methods.
- `self.attribute: Type` declares a typed instance attribute.
- Instance methods can read and modify their own attributes.
- Constructors use `__init__`.
- Inheritance allows classes to extend other classes.
- `super()` provides access to parent-class functionality.
- Classes compile into native C++ classes.
- No Python object model or Python runtime is required.

---

## 8. Structs

Lightweight, data-only containers that compile to `struct` with public members.

- Declared using `struct`
- No methods or inheritance
- Optional default values
- Explicitly typed members

Ideal for POD-like data transfer objects.

### Example

```python
struct Vector2:
    x: float = 0.0
    y: float = 0.0

def main() -> int:
    position: Vector2 = Vector2(10.0, 20.0)

    print(position.x)
    print(position.y)

    return 0
```

---

## 9. Templates

Supports **C++ templates** using Pythonic syntax for both classes and functions.

- Templates compile 1:1 to standard C++ templates
- Syntax simplified for readability
- Fully compatible with STL and user-defined types

### Generic Functions

Generic functions allow developers to write reusable code that accepts different types while maintaining static type safety.

```python
def print_value[T](value: T) -> void:
    print(value)

def main() -> int:
    print_value(100)
    print_value("Hello")
    print_value(3.14)

    return 0
```

These functions compile into native C++ templates.

Generic functions preserve compile-time type checking without requiring a dynamic object system or runtime type information.

---

## 10. Constexpr

Supports compile-time constants via `constexpr`.

Used for mathematical or configuration constants evaluated at compile-time.

```python
constexpr MAX_PLAYERS: int = 100
constexpr GAME_SPEED: float = 1.5
```

This allows developers to take advantage of C++ compile-time evaluation through Pythonic syntax.

---

## 11. Imports & Modules

### Pythonic Imports — *Only for PyCPP Files*

PyCPP supports Python-style imports for its own source files.

```python
from player import Player
import game_utils
```

- Modules are designed specifically for PyCPP source files.
- Imported symbols are resolved by the PyCPP compiler.
- No manual header management is required.
- The compiler handles necessary C++ declarations and dependencies internally.

### C/C++ Interoperability

PyCPP also supports direct interaction with existing C and C++ libraries.

```python
use <iostream>
use <vector>
```

The compiler automatically handles the required integration with native C++ headers.

---

## 12. Design Philosophy

PyCPP aims to combine three programming language philosophies:

- **Python:** Simple, readable syntax, natural object interaction, and approachable OOP.
- **Odin:** Explicit control over memory, references, pointers, and low-level operations.
- **C++:** Native compilation, performance, templates, and systems-level capabilities.

### The PyCPP Principle

> **Simple by default. Explicit when needed. Powerful without compromise.**

Developers should be able to write straightforward code without unnecessary complexity, while retaining access to the low-level capabilities expected from a systems programming language.

PyCPP is designed to make systems programming more approachable without sacrificing the control, predictability, and performance of native C++.

---

## Project Vision

PyCPP aims to make native systems programming feel natural.

You should be able to create objects, call methods, pass arguments, manage memory, work with collections, and build complex applications using readable Python-inspired syntax.

But underneath, everything compiles into real C++.

**No Python interpreter. No garbage collector. No mandatory runtime. Just native code.**

> **PyCPP — Pythonic to write. Native to run.**
