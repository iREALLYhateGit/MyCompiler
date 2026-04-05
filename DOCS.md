# Task 5 User Types

## New Syntax

The language now supports user-defined types and imported methods.

### Classes

```txt
class Point
var x: int;
    y: int;
begin
    public method sum(): int
    begin
        x + y;
    end;
end
```

### Single Inheritance

```txt
class ColoredPoint : Point
var color: int;
begin
    public method print()
    begin
        write(color);
    end;
end
```

### Interfaces

```txt
interface IPrinter
begin
    method print();
end
```

### Interface Implementation

```txt
class ConsolePoint implements IPrinter
var value: int;
begin
    public method print()
    begin
        write(value);
    end;
end
```

### Imported Methods

```txt
method imported(a: int): int from "native_add" in "native.dll";
```

Imported methods are stored in the semantic model. The current VM backend does not support foreign calls, so any real code generation that tries to execute an imported method reports an explicit backend error.

### Member Access

```txt
method main()
var p: Point;
begin
    p.x := 1;
    p.y := 2;
    write(p.sum());
end;
```

Supported member operations:

- field read: `p.x`
- field write: `p.x := 1`
- method call: `p.sum()`
- chained access: `obj.inner.value`

## Semantic Rules

- A class may inherit from at most one base class.
- A class may implement multiple interfaces.
- Interface inheritance is not supported.
- Method overloading is supported for class methods.
- Method overriding from a base class is forbidden.
- A class must implement every declared interface method.

## Backend Notes

- User-defined value types are lowered as flattened storage slots.
- Class methods are emitted as ordinary subprograms with a hidden receiver represented by flattened `this.*` slots.
- Member-call code generation resolves receiver types from declared variable/member metadata instead of relying only on flattened storage slots. This allows calls such as `p.sum()`, `ops.add(...)`, and inherited calls like `p.getX()` to reach ASM generation.
- When a member call resolves to a base-class method, the backend passes the receiver layout expected by the resolved callee type, not the full derived-class field set.
- Type metadata is emitted into a separate `[section TYPE_INFO]` section using zero-byte labels, because the remote assembler accepts section headers and labels but rejects custom directives such as `.type`, `.field`, and `.implements`.
- Metadata labels use these textual forms:
  - `TYPEINFO_type_class_Point_size_8:`
  - `TYPEINFO_field_Point_x_int_offset_0:`
  - `TYPEINFO_implements_ColoredPoint_IPrinter:`

## Test Assets

Acceptance scenarios for Task 5 are stored in:

- `tests/task5/run_task5_tests.ps1`
- `tests/task5/inputs`

Example programs are stored in:

- `examples/task5`
