# PyCPP diagnostic codes

Every error and warning PyCPP reports has a stable code, shown in brackets: `error[E0101]`, `warning[W0001]`.
Codes never change meaning. `pycpp --explain <code>` prints the same text as this page.

Warnings can be silenced on a line with `# allow: <name>` or `# allow: <code>` (see the README, *Diagnostics and tests*).
This file is generated from `PyCpp/include/error_codes.h`; edit that file, not this one.

## Syntax errors

### E0001 `unexpected-character`

**A character that is not part of PyCPP syntax.**

```
The lexer found a character that cannot start any token, such as `$` or `?`.
Remove it, or put it inside a string literal.
```

### E0002 `unterminated-string`

**A string literal is not closed before the end of the line.**

```
String literals must start and end on the same line:
    print("hello)     # error
    print("hello")    # ok
```

### E0003 `expected-colon`

**A block header is missing its ':'.**

```
`def`, `class`, `struct`, `if`, `elif`, `else`, `for`, `while`, `try`, `except`,
`finally`, `match` and `case` headers end with ':' followed by an indented block.
```

### E0004 `indentation`

**The indentation does not match the block structure.**

```
A block must be indented more than its header, and every line of a block must use
the same indentation (a tab counts as 4 spaces).
```

### E0005 `stray-token`

**Unexpected text after the end of a statement.**

```
PyCPP has one statement per line and no ';' separator:
    x: int = 1 y: int = 2    # error
Put each statement on its own line.
```

### E0006 `expected-expression`

**A value, name or call was expected.**

```
The parser needed an expression here (a literal, a name, a call, ...) but found
something else, or the line ended.
```

### E0007 `unclosed-bracket`

**A '(', '[' or '{' is not closed.**

```
Every opening bracket needs a matching closing bracket. Inside brackets a
statement may continue on the next lines.
```

### E0008 `invalid-assignment-target`

**The left side of '=' cannot be assigned to.**

```
Only a variable, a member (a.b), an element (a[i]) or a dereference (*p) can be
assigned to. `f() = 1` and `1 = x` are errors.
```

### E0009 `chained-comparison`

**Chained comparisons are not supported.**

```
`a < b < c` is not supported. Write `a < b and b < c`.
```

### E0010 `literal-out-of-range`

**An integer literal does not fit in 64 bits.**

```
Integer literals up to 9223372036854775807 are supported. Literals that do not fit
in `int` have type i64. Use a float literal for larger magnitudes.
```

### E0011 `unsupported-feature`

**A Python feature PyCPP does not implement.**

```
Some Python keywords (yield, async/await, with, global, nonlocal, ...) have no PyCPP
equivalent yet. The message names the feature and suggests an alternative.
```

### E0012 `expected-type`

**A type was expected.**

```
Write a type such as int, float, str, bool, list[int], dict[str, int], *Node or a
class name.
```

### E0013 `syntax`

**Other syntax error.**

```
The parser could not understand this line. The message describes what it expected.
```

### E0014 `bad-fstring`

**An f-string replacement field is malformed.**

```
Inside an f-string, `{expr}` or `{expr:spec}` inserts a value; `{{` and `}}` are
literal braces. Every '{' needs a matching '}' and the expression must not be empty:
    f"total: {n:5d}"   f"{{literal}}"
```

## Name and type errors

### E0101 `unknown-name`

**A name is used that was never declared.**

```
Variables, functions and classes must be declared before they are used. Check the
spelling; the message suggests the closest known name.
```

### E0102 `unknown-type`

**A type name is not known.**

```
The type is not a built-in type, and no class or struct of that name was defined or
imported. Declare it, or import it with `from module import Name`.
```

### E0103 `duplicate-definition`

**A name is defined twice in the same scope.**

```
Two declarations of the same name in one scope are not allowed. Rename one of them,
or assign without a type to reuse the existing variable: `x = 5`.
```

### E0104 `mismatched-types`

**A value of the wrong type is stored in a variable.**

```
Declarations and assignments must store a value of a compatible type. Arithmetic types
convert implicitly; anything else needs an explicit conversion such as int(x) or str(x).
```

### E0105 `mismatched-return`

**A returned value does not match the declared return type.**

```
`return value` must produce the type written after '->' in the function header.
```

### E0106 `wrong-argument-count`

**A call passes too many or too few arguments.**

```
The number of arguments must match the parameters of the function, method or
constructor (parameters with a default value may be left out).
```

### E0107 `mismatched-argument`

**An argument has the wrong type.**

```
Each argument must be compatible with the type of its parameter.
```

### E0108 `statement-outside-function`

**Executable code at the top level of a file.**

```
Only declarations (def, class, struct, imports, typed variables) may appear at the top
level. Put statements inside a function such as `def main() -> int:`.
```

### E0109 `return-outside-function`

**'return' outside a function.**

```
`return` can only be used inside a def.
```

### E0110 `misplaced-control-flow`

**A statement is used where it is not allowed.**

```
Examples: `defer`, `try` and `raise` outside a function; `return`, `break` or
`continue` inside a defer or finally block; a bare `raise` outside an except block.
```

### E0111 `bad-match`

**A match statement is malformed.**

```
Cases after `case _:` can never run, a value may appear in only one case, and every
pattern must have the type of the match subject.
```

### E0112 `unknown-member`

**A method or attribute does not exist on a class.**

```
The class (and its base classes) define no method or attribute of that name. The
message suggests the closest existing one.
```

### E0113 `bad-super`

**Super() is used incorrectly.**

```
`super()` refers to the base class of the class being defined, so it can only be
used inside a method of a class that has a base class. In __init__,
`super().__init__(...)` must be the first statement, because C++ constructs the base
class before the derived one, and its arguments cannot use `self`.
```

### E0114 `cannot-infer`

**The type of a variable cannot be inferred.**

```
`x = value` declares x with the type of value. Some values carry no element type
(`[]`, `{}`, `list()`, `dict()`, `None`) or no value at all (a call to a function that
returns void); declare those explicitly: `x: list[int] = []`.
```

### E0115 `not-callable`

**Something that is not a function is called.**

```
Only functions, methods, classes (constructors) and lambdas can be called.
```

## Module and build errors

### E0201 `module-not-found`

**An imported module cannot be found.**

```
Modules are looked up next to the importing file and in the -I search paths, as
<name>.pycpp or <name>/__init__.pycpp.
```

### E0202 `no-main`

**The entry file has no main function.**

```
The file passed to pycpp must define `def main() -> int:`.
```

### E0203 `module-name-clash`

**Two modules would generate the same C++ files.**

```
Two source files map to the same generated name. Rename one of them.
```

### E0204 `cpp-compile-failed`

**The generated C++ did not compile.**

```
PyCPP did not detect a problem but the C++ compiler did. Re-run with --verbose to see
the compiler output. If the PyCPP code is valid, this is a PyCPP bug.
```

### E0205 `link-failed`

**Linking failed, or a C++ header is missing.**

```
A function from a C/C++ library is used but the library is not linked (pass it with
--linker-flags "-l<name>"), or a `use <header>` names a header that is not installed.
```

## Command-line errors

### E0301 `bad-option`

**An unknown or incomplete command-line option.**

```
Run `pycpp --help` for the list of options.
```

### E0302 `cannot-open`

**An input file cannot be opened.**

```
Check the path, and the directory pycpp is run from.
```

### E0303 `no-compiler`

**The selected C++ compiler is not available.**

```
Install it, put it on PATH, or choose another one with --compiler.
```

## Warnings

### W0001 `unused`

**A local variable is never used.**

```
The variable is declared but never read. Remove it, or prefix its name with '_'
(`_tmp`) to show that this is intentional.
```

### W0002 `unreachable`

**Code after return, break, continue or raise.**

```
Statements after a return/break/continue/raise in the same block can never run.
```

### W0003 `missing-return`

**A function can end without returning a value.**

```
A function with a return type must return a value on every path. Falling off the end
of a non-void function is undefined behaviour in C++. Paths that end in `raise`, or
loops such as `while True:` that never finish, do not need a return.
```

### W0004 `float-to-int`

**A float value is stored in an integer.**

```
The fractional part is lost: `x: int = 2.5` stores 2.
```

### W0005 `mismatched-operands`

**An operator is used on values of unrelated types.**

```
For example `"a" + 1`. The C++ compiler is likely to reject it.
```

### W0006 `no-member`

**A member of self is used but not declared.**

```
Declare members in __init__ with `self.name: type = value`.
```

### W0007 `unknown-exception`

**An except clause names an unknown exception type.**

```
Use a built-in exception or define your own with `class MyError(Exception):`.
```

### W0008 `division`

**True division '/' where an integer is expected.**

```
In PyCPP, as in Python, `/` always produces a float (`7 / 2` is 3.5). Storing that in
an integer, using it as an index or passing it to an int parameter drops the
fractional part. Use `//` for integer (floor) division. `x /= y` on an integer x is
integer division in the generated C++.
```

### W0009 `index-type`

**A list index is not an integer.**

```
List indices must be integers.
```

### W0010 `extension`

**The input file does not end in .pycpp.**

```
PyCPP source files are named <name>.pycpp.
```
