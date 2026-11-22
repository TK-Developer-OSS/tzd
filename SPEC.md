# TzD — Project Specification

**Version:** 0.1-draft  
**Status:** Concept definition  
**Project type:** GCC-based native system programming language

---

## 1. Project Vision

TzD is a C-compatible system programming language designed to bring managed-language safety and modern programming facilities to existing C software.

Its primary objective is:

> Recompile existing C projects with TzD and run them as Managed C, without requiring wholesale source-code rewrites.

TzD preserves the familiar C programming model while introducing runtime safety, object-oriented programming, contracts, delegates, and other higher-level language facilities.

The initial practical target is **Apache HTTP Server 1.3**.

Apache 1.3 is not merely a compilation test. It is the first substantial demonstration that TzD can preserve and improve the value of existing C software.

## 2. Design Philosophy

### 2.1 Preserve C

C syntax and programming conventions are the foundation of TzD.

- Return types precede function names.
- Function declarations use `name(arguments)`.
- Blocks use `{}`.
- Pointer declarations retain C-style notation.
- Existing C source code should require minimal modifications.
- C ABI compatibility is a fundamental requirement.

Avoid introducing unnecessary syntax such as `fn` or `func`.

A function declaration already has a recognizable structure:

```tz
int main(int argc, char **argv) {
    return 0;
}
```

The language should not force programmers to learn a new declaration convention without a compelling technical reason.

### 2.2 Improve C Rather Than Replace It

TzD should retain the expressive power of C while improving its weaknesses.

The language should provide:

- Runtime memory safety checks.
- Managed pointers.
- Object-oriented programming.
- Exception handling.
- Design by Contract.
- Delegates.
- C library interoperability.

Low-level programming must remain possible. Safety mechanisms should not make systems programming impractical.

### 2.3 Minimize Syntactic Noise

New features should integrate naturally into familiar C syntax.

Prefer readable keywords over punctuation-heavy constructions when they improve clarity.

The goal is to make TzD understandable to experienced C programmers without imposing an entirely new programming style.

### 2.4 File Extensions and Language Modes

The source file extension selects the language mode.

| Extension | Mode | Meaning |
|---|---|---|
| `.c` | Managed C | C source compiled with managed runtime checks. TzD extensions (classes, contracts, delegates, etc.) are not available. |
| `.d` | TzD | Full TzD, including all extensions. |

- `.c` and `.d` share one ABI: a `.c` object and a `.d` object can be linked together and call each other directly. Managed C is therefore a strict subset of TzD at the ABI level.
- Existing C projects are recompiled as `.c` with no renames. New or migrated code can adopt TzD extensions file by file by switching to `.d`.
- Both modes use the same front end, the same managed-pointer runtime and the same data layouts; the extension only enables or disables the extension syntax.
- Note: `.d` is also the extension of the D language. Tooling must not assume that a `.d` file is D source. Whether to keep `.d` or add an alias (e.g. `.tzd`) remains open.

---

## 3. Compiler Architecture

TzD will be implemented as a GCC language front end.

The intended compilation pipeline is:

```text
TzD Source
    |
    v
TzD Front End
    |
    v
GCC GENERIC
    |
    v
GCC GIMPLE
    |
    v
GCC Middle End
    |
    v
GCC Backend
    |
    v
Native Object Code
    |
    v
.o / Executable
```

GCC will provide:

- Target architecture support.
- Language-independent optimization.
- Native code generation.
- Object file generation.
- Integration with the GNU toolchain.

The project will follow the GCC front-end model rather than implementing an independent assembler or machine-code backend.

The initial implementation should study GDC's GCC integration architecture while keeping the TzD front end independently maintainable.

GCC internal interfaces are version-sensitive. The initial implementation should therefore pin a specific GCC version.

## 4. Managed C

Managed C is the central feature of TzD.

Existing C programs should be compilable under TzD, with managed runtime facilities added where their semantics permit.

### 4.1 Managed Memory

Memory allocations should carry sufficient metadata to enable runtime checks.

The runtime should be able to track:

- Allocation boundaries.
- Allocation size.
- Object type and size where available.
- Pointer validity.
- Deallocation state.
- Pointer-derived access ranges.

### 4.2 Boundary Checking

Memory accesses outside the valid allocation range should raise an exception.

Example:

```tz
int *p = allocate<int>(10);

p[9] = 100;   // Valid
p[10] = 100;  // Boundary violation
```

Pointer operations must be checked at the point where an actual memory access occurs.

For example:

```tz
*p += 100;
```

The operation should verify that the target memory access is within the valid allocation.

### 4.3 Managed Pointer Objects

TzD pointers are conceptually objects, not merely raw C addresses.

The intended model is based on a `Pointer` class with facilities such as:

- Pointer arithmetic.
- Dereferencing.
- Indexing.
- Boundary checking.
- Validity checking.
- Allocation tracking.
- Operator methods.

Illustrative concept:

```tz
class Pointer<T> {
    // Native address and allocation metadata

    Pointer<T> operator +(int offset);
    Pointer<T> operator -(int offset);

    bool isValid();
}
```

C-like pointer syntax remains available:

```tz
Hello *h2 = &hello;

convertFunc(h2);
```

Both explicit address-taking and compatible implicit conversion are intended to be supported.

The recommended notation is explicit:

```tz
Hello *h2 = &hello;
```

### 4.4 Native Pointer Interoperability

Native pointers remain necessary for C ABI compatibility and external libraries.

TzD must distinguish between:

- Managed pointers with known allocation metadata.
- Native pointers obtained from external C code.
- Pointers whose allocation boundaries cannot be established.

The runtime must not claim safety guarantees for arbitrary external addresses without sufficient metadata.

The exact conversion and checking rules remain to be specified.

---

## 5. Language Features

### 5.1 C-Compatible Syntax

TzD should support conventional C constructs, including:

- Functions.
- Structures.
- Arrays.
- Pointers.
- Function pointers.
- Typedefs.
- Preprocessor directives.
- C-compatible data layouts.
- C ABI calls.

The initial compatibility target is existing C software, not merely a newly written C-like subset.

### 5.2 Classes and Inheritance

TzD will support classes and object-oriented programming.

Illustrative syntax:

```tz
abstract class Message {
    private string msg = "Hello";

    string getMessage();
}

public class HelloWorld extends Message {
    string getMessage() {
        return msg + "World!" + EOL;
    }
}
```

Inheritance uses the explicit keyword `extends`, rather than the C++ colon notation.

This choice prioritizes readability and avoids overloading the colon with additional syntax roles.

### 5.3 C-Compatible Function Declarations

Functions retain the C declaration style.

```tz
int divide(int a, int b) {
    return a / b;
}
```

No `fn` or `func` prefix is required.

### 5.4 Design by Contract

TzD will consider incorporating D-style Design by Contract.

The proposed syntax is:

```tz
int divide(int a, int b) {
    in:
        assert(b != 0);

    out: result
        assert(result * b == a);

    body:
        return a / b;
}
```

The sections mean:

| Section | Meaning |
|---|---|
| `in:` | Preconditions |
| `out: result` | Postconditions; `result` represents the return value |
| `body:` | Function implementation |

Ordinary functions without contracts retain normal C-style syntax.

Contract checking should be configurable for development, testing, and production builds.

Class invariants are also candidates for inclusion.

### 5.5 Delegates

D-style delegates are candidates for inclusion.

A delegate represents a callable function together with its execution context.

Conceptually:

```c
struct Delegate {
    void *context;
    void (*function)(void *context);
};
```

The implementation should avoid unnecessary dependence on garbage collection and should remain compatible with native function-pointer interoperability where possible.

---

## 6. Compatibility and Initial Target

### 6.1 Apache HTTP Server 1.3

The initial major acceptance target is successful compilation of Apache 1.3.

The goal is to establish that TzD can handle a substantial real-world C codebase.

The compatibility investigation must include:

- C language dialect and compiler extensions.
- Preprocessor behavior.
- Structure layouts.
- Function pointers.
- Pointer arithmetic.
- Memory allocation.
- Custom memory pools.
- System and library interfaces.
- Build-system assumptions.
- C ABI compatibility.

The project should not assume that supporting standard C syntax alone is sufficient.

### 6.2 Compatibility Strategy

The first milestone should prioritize source compatibility.

Existing C code should compile with minimal or no modifications.

Managed behavior should be introduced through compiler transformations and runtime support, with explicit compatibility boundaries where complete checking is impossible.

The project must distinguish between:

1. Successfully compiling C source.
2. Successfully linking existing libraries.
3. Executing the program correctly.
4. Enforcing managed memory safety.

These are separate milestones and should be tested separately.

---

## 7. Development Principles

### 7.1 Keep the Front End Small

The project should focus on the language front end and managed runtime mechanisms.

GCC should handle optimization and target-specific code generation.

Avoid duplicating functionality already provided by GCC.

### 7.2 Keep AI Development Manageable

The codebase must be organized to minimize the amount of context required for individual development tasks.

Principles:

- Small, well-defined modules.
- Explicit interfaces.
- Limited cross-module dependencies.
- Localized changes.
- Automated regression tests.
- Clear language specifications.
- Avoid unnecessary framework dependencies.

A proposed initial front-end size target is approximately 15,000–25,000 lines of code. This is a planning target, not an implementation estimate.

### 7.3 Test Against Real Software

Synthetic examples alone are insufficient.

The project should maintain:

- Small language feature tests.
- C compatibility tests.
- ABI tests.
- Runtime safety tests.
- Memory boundary tests.
- Apache 1.3 compilation tests.
- Apache execution tests.

---

## 8. Initial Development Milestones

| Milestone | Objective |
|---|---|
| M0 | Select GCC base version and establish build environment |
| M1 | Minimal TzD GCC front end |
| M2 | C-compatible function and type declarations |
| M3 | Basic C pointer and memory support |
| M4 | C preprocessor and compatibility support |
| M5 | C ABI and libc integration |
| M6 | Compile progressively larger C projects |
| M7 | Compile Apache 1.3 |
| M8 | Introduce managed pointer runtime |
| M9 | Demonstrate boundary violation exceptions |
| M10 | Introduce classes, inheritance, contracts, and delegates |

The exact ordering may change as Apache compatibility requirements become clearer.

---

## 9. Unresolved Design Decisions

The following items are intentionally not finalized.

### Memory Model

- Whether classes are reference types and structs are value types.
- Whether garbage collection is supported.
- Allocation and deallocation semantics.
- Stack, heap, and global object handling.
- Lifetime tracking.

### Pointer Model

- Exact representation of `Pointer<T>`.
- Native pointer conversion rules.
- Pointer arithmetic semantics.
- Behavior of pointers to stack and global objects.
- Handling of custom allocators and memory pools.
- Treatment of arbitrary external C pointers.

### Safety Model

- Default safety level.
- Explicit unsafe operations.
- Runtime checking modes.
- Exception behavior.
- Handling of use-after-free and invalid pointer conversions.

### Object Model

- Virtual methods.
- Multiple inheritance or interfaces.
- Constructor and destructor semantics.
- Operator overloading rules.

### Runtime

- Minimum runtime dependencies.
- Exception implementation.
- Allocation metadata storage.
- Performance overhead.
- Integration with libc and existing C libraries.

These decisions should be made incrementally, based on implementation requirements and real compatibility tests.

---

## 10. Project Definition

TzD is not intended to be another language that asks C programmers to abandon their existing code and programming habits.

Its central proposition is:

**Keep C's syntax and capabilities. Add the safety and abstraction facilities that C never had.**

The first proof is Apache 1.3.

The long-term objective is to make existing C software progressively safer and more maintainable simply by adopting a different compiler.
