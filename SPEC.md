# C-+- Language Spec

Working spec for C-+-, a compiled language derived from C++, part of System Design[Scratch].
The compiler is written in C++. This file records decisions so they survive between sessions.

Status key: **Decided** = settled. **Open** = still to be decided.

## Hello World

```
import <io>

func main(){
    Type["Hello World!"], \n
    return 0;
}
```

## Comments

- **Decided:** `//` starts a comment that runs to the end of the line.
- **Open:** block comments (`/* ... */`). Not added for now.

## Imports

- **Decided:** `import <io>` at the top of a file.

## Output: `Type`

- **Decided:** `Type[...]` is an output statement (like `cout`). It is not a function call.
- **Decided:** inside `[ ]` is the list of things to print. `"..."` is literal text, a bare name prints that variable's value.
  Example: `Type["Hello," name], \n`
- **Decided:** a trailing `, \n` adds a newline. Without it, no newline is printed.
- **Open:** items inside `[ ]` are separated only by a space. How expressions are written inside (e.g. `a -b`: one item or two?) is not decided.

## Input: `Read`

- **Decided:** `Read[x]` reads input into the variable `x`.
- **Open:** exact behavior (one word or a whole line, what happens on bad input).

## Types

- **Decided:** `Integer`, `Boolean`, `String`, `Char`.
- **Decided:** `DC_number` is dropped (it was in the old token list, its meaning is lost).
- **Open:** `true` / `false` as Boolean literals (assumed, not confirmed). Decimal numbers.

## Variables

- **Decided:** `Integer x = 5` and `String name = "Saad"`.
- **Decided:** a semicolon is not required at the end.

## Functions

- **Decided:** `func add(Integer a, Integer b) -> Integer { ... }`.
- **Decided:** a function with no `->` returns nothing, except `main`, which always returns `Integer`.

## Statement endings

- **Decided:** `;` is not required everywhere.
- **Open:** the exact rules for when `;` is required, optional, or forbidden. (`return 0;` currently uses one.)

## Control flow

- **Decided:** C++ style, with parentheses and braces.

```
if (x > 3) { ... } else if (x > 1) { ... } else { ... }
while (x < 10) { ... }
for (Integer i = 0; i < 10; i = i + 1) { ... }
```

- `else if` is written as two words (the lexer emits `else` then `if`; the parser combines them).

## Operators

- **Decided:** math `+ - * / %`, comparison `== != < > <= >=`, logic `&& || !`, assignment `=`.
- The lexer still needs the two-character ones (`==`, `!=`, `<=`, `>=`, `&&`, `||`) and `%` and `!`.

## Lexer to-do (follows from this spec)

- Add keywords: `Read`, `String`, `Char`, `while`, `for`, `if`, `else`.
- Remove `DC_number` from the token list; rename `Character` to `Char`.
- Add the two-character operators and `%`, `!`.
- Escapes inside strings (`\t`, `\\`, `\"`).

## Memory

- **Decided:** no garbage collector.
- **Decided:** scope-based cleanup. A value that owns memory is freed automatically when its scope ends. No full borrow checker.
- **Open:** copy and move rules. What happens when one owning value is assigned to another.

## Ternary
- **Decided:** `condition ? a : b` picks `a` if the condition is true, otherwise `b`.
- **Decided:** items inside `Type[ ]` are a string literal, a name, or an expression in parentheses (this settles the earlier open question).

## Assignment

- **Decided:** `x = value` assigns to an existing variable.
- **Decided:** `x <- value` is a second spelling of the same assignment, meant for updating a variable from its old value, e.g. `score <- score + 1`. It behaves exactly like `=`.
- **Open:** whether the compiler should require the right side of `<-` to mention the variable being updated.
- **Note:** `<-` is one token, so `a<-b` always reads as the arrow, never "a less than minus b".

## Types (updated)

- **Decided:** `Int`, `Char`, `Boolean`, `String`, `Deci` (decimal numbers). `Int` replaces `Integer`.

## Declarations (updated)

- **Decided:** several names in one declaration: `Int x, y, z`.
- **Decided:** a single `= value` at the end applies to all the names, so `Int x, y, z = 0` sets all three to 0 (unlike C++, where only z would be set).

## Control flow (updated)

- **Decided:** `if` / `else if` / `else`, `while`, `for`, and `switch` / `case` / `default`.
- **Decided:** `switch` has no fall-through. Each case runs only its own statements, so no `break` is needed.
- **Decided:** `break` and `continue` exist, for loops.
- **Open:** inside a `switch` that is inside a loop, does `break` leave the loop (proposed) or only the switch?
- **Decided:** the three parts of a `for` header are separated by `;`.
