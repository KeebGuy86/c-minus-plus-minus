# C-+- Language Spec

Working spec for C-+-, a compiled language derived from C++, part of System Design[Scratch].
The compiler is written in C++. This file records decisions so they survive between sessions.

Scope: the basics only (variables, operators, control flow, functions, arrays). No OOP, and not meant to be industrial grade.

Status key: **Decided** = settled. **Open** = still to be decided.

## Hello World

```
import <io>

func main(){
    Type["Hello World!"], \n
    return 0
}
```

## Comments

- **Decided:** `//` starts a comment that runs to the end of the line.
- **Open:** block comments (`/* ... */`). Not added for now.

## Imports

- **Decided:** `import <io>` at the top of a file.

## Types

- **Decided:** `int`, `char`, `string`, `bool`, `deci` (decimal numbers), all lowercase. (`DC_number` was dropped.)
- **Decided:** string literals use double quotes, with the escapes `\n`, `\t`, `\\` and `\"`.
- **Decided:** character literals use single quotes: `'a'`, `'+'`, `'\n'`.
- **Open:** `true` / `false` as the bool values (assumed, not confirmed). How a bool prints (currently as 1 or 0).
- **Open:** how decimal numbers are written (`3.14`) and how `deci` works.

## Variables and declarations

- **Decided:** `int x = 5`, `string name = "Saad"`. A semicolon is not required.
- **Decided:** several names in one declaration: `int x, y, z`.
- **Decided:** a single `= value` or `<- value` at the end applies to all the names, so `int x, y, z = 0` sets all three to 0 (unlike C++, where only z would be set). `int a = 1, b = 2` is an error.
- **Open:** a variable declared without a value starts at 0 in the current compiler. Not yet confirmed as the rule.

## Assignment

- **Decided:** `x = value` assigns to an existing variable.
- **Decided:** `x <- value` is a second spelling of the same assignment, meant for updating a variable from its old value, e.g. `score <- score + 1`. It behaves exactly like `=`.
- **Decided:** assignment is a statement, not an expression, so `x = y = 3` is not allowed.
- **Open:** whether the compiler should require the right side of `<-` to mention the variable being updated.
- **Note:** `<-` is one token, so `a<-b` always reads as the arrow, never "a less than minus b".

## Operators and expressions

- **Decided:** math `+ - * / %`, comparison `== != < > <= >=`, logic `&& ||`.
- **Decided:** precedence, tightest first: `* / %`, then `+ -`, then `< > <= >=`, then `== !=`, then `&&`, then `||`. Equal operators group from the left.
- **Decided:** `&&` and `||` short-circuit: the right side is not evaluated when the left side already decides the answer.
- **Decided:** the ternary `condition ? a : b` picks `a` if the condition is true, otherwise `b`. It binds weaker than every operator and chains to the right: `a ? b : c ? d : e` means `a ? b : (c ? d : e)`.
- **Decided:** a newline does not end an expression. An operator on the next line continues the previous one, so `5` then `-3` on the next line means `5 - 3`.
- **Open:** unary operators (`-x`, `!x`). They don't exist yet.
- **Open:** if a statement type is ever added that can begin with an operator character (`-`, `(`, `[`), the newline rule needs revisiting.
- **Open:** what happens on division by zero (it currently crashes the program).

## Output: `Type`

- **Decided:** `Type[...]` is an output statement (like `cout`). It is not a function call.
- **Decided:** inside `[ ]` is the list of things to print, separated only by spaces. Each item is a string literal, a variable name, an array element, a call, or an expression in parentheses.
  Example: `Type["Hello," name (a + 1)], \n`
- **Decided:** a trailing `, \n` adds a newline. It is optional: without it, no newline is printed.
- **Decided:** `\n` means "a newline" in the abstract. The compiler chooses the right bytes for the target system.
- **Decided:** the `, \n` form is kept. A named keyword (`Newline`) was considered and not adopted.

## Input: `Read`

- **Decided:** `Read[x]` reads what the user types into the variable `x`. `Read[nums[i]]` reads into an array element.
- **Decided:** reading into an `int` reads a whole number, and into a `char` reads one character.
- **Open:** reading into a `string`. What happens on bad input.

## Control flow

- **Decided:** `if (cond) ... else if (cond) ... else ...`, `while (cond) ...`, `for (init; cond; update) ...`, and `switch` / `case` / `default`. Conditions always need parentheses.
- **Decided:** braces are optional when the body is a single statement. `else` binds to the nearest `if`.
- **Decided:** the three parts of a `for` header are separated by `;`, and any of them may be empty.
- **Decided:** `switch` has no fall-through. Each case runs only its own statements, so no `break` is needed. One body can serve several values: `case 'a', 'b':`. A switch can have at most one `default`.
- **Decided:** `break` applies to loops only, and is an error outside one. Inside a `switch` that is inside a loop, `break` leaves the loop.
- **Decided:** `continue` is allowed anywhere. Inside a loop it skips to the next turn; outside a loop it does nothing.
- **Decided:** a variable declared inside a `{ }` block stops existing at the closing brace.
- **Open:** declaring a name that already exists in an outer scope (currently an error).

## Functions

- **Decided:** `func add(int a, int b) { ... }`. A function with no `->` is a procedure and returns nothing.
- **Decided:** `func twice(int n) -> int { return n * 2 }`. The return type is written after the parameters with an arrow, and `return value` hands a value back.
- **Decided:** a bare `return` (nothing after it on the line) leaves a procedure early.
- **Decided:** `main` always returns an `int`.
- **Decided:** a call is `name(args)` with no space before the `(`. It works as an expression (`twice(3)`) or as a statement (`add(a, b)`).
- **Decided:** functions can be defined in any order.
- **Open:** how arguments are passed (assumed: by value, each call gets its own copy).

## Arrays

- **Decided:** `int num[5]` declares an array. The size is a number written in the code. Several names are allowed: `int a[3], b, c[2]`.
- **Decided:** elements are numbered from 1. `int big[100]` has elements `big[1]` to `big[100]`.
- **Decided:** `num[i]` reads an element, `num[i] = 7` and `num[i] <- 7` write one, and `Read[num[i]]` reads input into one.
- **Decided:** starting values in a list: `int num[5] = [1, 2, 3, 4, 5]`, filling from element 1. Either arrow works. A list with more values than the array has room for is an error.
- **Decided:** a single starting value goes to the element named in the brackets: `int big[100] <- 5` sets `big[100]`, and `int a[2], b = 9` sets `a[2]` and `b`.
- **Decided:** every element not given a value starts at 0.
- **Decided:** a `[ ... ]` list can only be used to start an array. It is not a value you can store or pass around.
- **Open:** bounds checking (what happens with `num[0]` or `num[10]` on a size-5 array; currently nothing is checked).

## Memory

- **Decided:** no garbage collector.
- **Decided:** scope-based cleanup. A value that owns memory is freed automatically when its scope ends. No full borrow checker.
- **Open:** copy and move rules: what happens when one owning value is assigned to another. To be decided when the first type that owns memory is added.

## Compiler pipeline

Source text goes through the lexer (tokens), the parser (a tree), and the code generator (x86-64 assembly for NASM on Windows, using `printf` and `scanf` from the C library). NASM and the MinGW `gcc` turn that into a program.

Code generator status: Hello World, `int` / `char` / `bool` variables, assignment, `+ - * / %`, comparisons, `&&` and `||`, `Read`, `if` / `else` and `while`, with arrays added next. Still to do: functions, `switch`, `for`, `break` / `continue`, the ternary, `string` variables and `deci`.