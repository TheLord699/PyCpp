# **PyCPP — Pythonic Syntax, Native C++ Power**

> **Write like Python. Compile like C++. Run at native speed.**

---
 
##  Overview

**PyCPP** is a statically typed, indentation-based programming language that compiles directly to **modern C++**.  
It combines the **simplicity and readability of Python** with the **performance and control of C++**, without any runtime overhead.

###  Core Idea

> If C++ can do it, **PyCPP can too** — but with **cleaner, more Pythonic syntax** and **automatic header and memory management**.

---

##  Highlights

- **Pythonic syntax**, **C++ semantics**
- **Static typing** — no runtime overhead
- **Native STL integration** — `list`, `dict`, `str` backed by `std::vector`, `std::map`, `std::string`
- **Smart Include System** — automatically includes only standard libraries when needed
- **Headerless design** — no `.h` or `.hpp` files
- **Full C/C++ interop** via `use <header>`
- **Manual memory control** with optional unsafe blocks
- **Compile-time constants**, **templates**, **structs**
- **Native performance** equal to compiled C++

---

##  1. Syntax

PyCPP uses indentation to define blocks — no braces `{}` or semicolons `;`.  
Its syntax closely resembles Python, while enforcing C++-like static typing.

- `def` for functions  
- `class` and `struct` for types  
- Python-style control flow (`if`, `elif`, `else`, `for`, `while`)  
- `break`, `continue`, and `return` all supported  
- Automatic `return 0` in `main()` if omitted  

Future enhancements include **list comprehensions** and **slicing**.

---

##  2. Typing System

PyCPP is fully **statically typed**, with every variable, function argument, and return type declared explicitly.

Supports:

- Primitive types (`int`, `float`, `double`, `bool`, `str`)
- STL-backed types (`list[T]`, `dict[K,V]`)
- Pointer types (`*T`)
- Type aliases:
  - Integers: `i8`, `i16`, `i32`, `i64`
  - Floats: `f32`, `f64`

Type-safe operations are enforced at compile-time.  
Optional **type inference** is planned for the future.

---

##  3. Memory Management

PyCPP gives full manual control over memory allocation — no garbage collector.

- Allocate and free memory manually using `mem.alloc()` and `mem.free()`
- Perform pointer arithmetic inside `unsafe:` blocks
- Outside unsafe blocks, strict safety checks are enforced
- Future versions will introduce **RAII wrappers** for safer automatic memory cleanup

---

##  4. Control Flow

Supports all common control flow structures with Python-like syntax:

- `if`, `elif`, `else`
- `while`, `for`
- `break`, `continue`

Future versions will add **exception handling** (`try/except  try/catch`).

---

##  5. Functions

Defined using Python-style syntax with **required return types**.  
Default parameters and native C++ type returns are supported.

- Functions cannot be overloaded yet (planned)
- Pythonic function bodies  standard C++ function generation

---

##  6. Collections

Pythonic syntax backed by the C++ STL.

- `list[T]`  `std::vector<T>`
- `dict[K,V]`  `std::map<K,V>`

Supports:

- Direct iteration (`for x in nums:`)
- Key/value iteration (`for k, v in items.items():`)
- Automatic header detection and inclusion

Future enhancements include **slicing** and **comprehensions**.

---

##  7. Classes (Headerless OOP)

Defines classes using Pythonic syntax, compiled to valid C++ classes.  
No manual header management — PyCPP auto-generates everything internally.

- Instance variables are typed inside `__init__`
- Inheritance is supported
- No manual `.h` or `.hpp` files
- Seamless STL integration

---

##  8. Structs

Lightweight, data-only containers that compile to `struct` with public members.

- Declared using `struct`
- No methods or inheritance
- Optional default values

Ideal for POD-like data transfer objects.

---

##  9. Templates

Supports **C++ templates** using Pythonic syntax for both classes and functions.

- Templates compile 1:1 to standard C++ templates
- Syntax simplified for readability
- Fully compatible with STL and user-defined types

---

##  10. Constexpr

Supports **compile-time constants** via `constexpr`.

Used for mathematical or configuration constants evaluated at compile-time.

---

##  11. Imports & Modules

### Pythonic Imports — *Only for PyCPP Files*

```python
from player import Player
import game_utils
