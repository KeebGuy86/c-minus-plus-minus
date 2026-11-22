#pragma once

#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>
#include "ast.h"

// The code generator walks the tree (like print() does) but writes ASSEMBLY text
// instead of printing the tree. NASM turns that text into machine code.
//
// Windows x64 rules we follow (they decide which registers hold what):
//   - the first argument to a function goes in RCX, the second in RDX
//   - the caller must reserve 32 bytes of "shadow space" on the stack before a call
//   - the stack pointer (RSP) must be a multiple of 16 at the moment of every call
//   - a function's result comes back in EAX
//
// How values move around: every expression is computed into the register EAX.
// A variable lives in a "slot" on the stack, at a fixed distance below RBP.
class CodeGen
{
public:
    // Turns a whole program tree into the text of an assembly file.
    std::string generate(const Program& program) {
        // First pass: note down every function (its parameters and return type).
        for (const auto& item : program.items) {
            if (auto* fn = dynamic_cast<const FunctionDecl*>(item.get())) {
                if (funcs.count(fn->name)) {
                    throw std::runtime_error("the function '" + fn->name + "' is defined twice");
                }
                funcs[fn->name] = FuncInfo{fn->params, fn->returnType};
            }
        }
        if (!funcs.count("main")) {
            throw std::runtime_error("the program needs a function called main");
        }

        // Second pass: generate the code for each function.
        for (const auto& item : program.items) {
            if (auto* fn = dynamic_cast<const FunctionDecl*>(item.get())) {
                genFunction(*fn);
            }
        }

        // Put the file together: a header, the data section, then the code section.
        std::ostringstream out;
        out << "default rel                  ; address things relative to the code\n";
        out << "global main                  ; the program starts at main\n";
        out << "extern printf                ; printf comes from the C library\n";
        out << "extern scanf                 ; scanf reads what the user types\n\n";
        out << "section .data\n";
        out << "fmt_s  db \"%s\", 0            ; printf format: print a string\n";
        out << "fmt_d  db \"%d\", 0            ; printf format: print an int\n";
        out << "fmt_nl db 10, 0              ; printf format: just a newline (10)\n";
        out << "fmt_c  db \"%c\", 0            ; printf format: print a character\n";
        out << "fmt_in_c db \" %c\", 0         ; scanf format: read a character (the space skips a leftover Enter)\n";
        out << data.str();
        out << "\nsection .text\n";
        out << text.str();
        return out.str();
    }

private:
    std::ostringstream data;    // the .data section: text the program prints
    std::ostringstream text;    // the .text section: finished functions
    std::ostringstream body;    // the instructions of the function being generated right now
    int stringCount = 0;        // used to give every string its own name: str0, str1, ...
    int labelCount = 0;         // used to give every jump target its own name: .L0, .L1, ...

    // What we remember about each variable of the current function.
    struct Variable {
        std::string type;       // "int" ...
        int offset;          // how many bytes below RBP its slot starts (slot is at [rbp-offset])
        int count = 0;
    };
    std::map<std::string, Variable> vars;   // name -> variable, for the current function
    int localBytes = 0;           // how much stack the variables use so far

    struct LoopLabels{
        std::string next;
        std::string end;
    };
    std::vector<LoopLabels> loops;

    // What we know about each function, collected BEFORE any code is generated, so a
    // function can be called from code that comes earlier in the file than its definition.
    struct FuncInfo {
        std::vector<Param> params;
        std::string returnType;     // "" for a procedure (returns nothing)
    };
    std::map<std::string, FuncInfo> funcs;
    const FunctionDecl* currentFn = nullptr;    // the function we are generating right now

    // Windows passes the first four arguments in these registers, in this order.
    static constexpr const char* argRegs[4] = {"ecx", "edx", "r8d", "r9d"};

    // ---------- Functions ----------

    static std::string funcLabel(const std::string& name) {
        return name == "main" ? "main" : "fn_" + name;
    }

    void genFunction(const FunctionDecl& fn) {
        // Each function starts with no variables and an empty body.
        vars.clear();
        localBytes = 0;
        body.str("");
        body.clear();
        currentFn = &fn;

        if (fn.params.size() > 4) {
            throw std::runtime_error("the function '" + fn.name +
                                     "' has more than 4 parameters, which is not supported yet");
        }

        // Parameters arrive in registers. We copy each one into a slot of its own, so
        // from then on it behaves like any other variable of the function.
        for (size_t i = 0; i < fn.params.size(); i++) {
            const Param& p = fn.params[i];
            if (p.typeName != "int" && p.typeName != "char" && p.typeName != "bool") {
                throw std::runtime_error("only int, char and bool parameters are supported so far");
            }
            allocate(p.name, p.typeName);
            body << "    mov dword " << slotOf(lookup(p.name)) << ", " << argRegs[i]
                 << "      ; parameter " << p.name << "\n";
        }

        // Generate the body FIRST, into 'body'. We can't write the start of the
        // function yet, because it needs to know how much stack the variables use,
        // and we only find that out by going through the whole body.
        for (const auto& stmt : fn.body) {
            genStatement(*stmt);
        }

        // Stack space = the variables + 32 bytes of shadow space for calls,
        // rounded UP to a multiple of 16 so RSP stays aligned.
        int frame = localBytes + 32;
        frame = (frame + 15) / 16 * 16;

        text << funcLabel(fn.name) << ":\n";
        text << "    push rbp                 ; save the caller's frame pointer\n";
        text << "    mov rbp, rsp             ; our frame starts here\n";
        text << "    sub rsp, " << frame << "              ; room for our variables and for the calls we make\n";
        text << body.str();

        // Falling off the end of a function returns 0. A 'return' statement
        // jumps straight to .exit with its own value already in EAX.
        text << "    xor eax, eax             ; default return value: 0\n";
        text << ".exit:\n";
        text << "    mov rsp, rbp             ; throw away our stack space\n";
        text << "    pop rbp                  ; restore the caller's frame pointer\n";
        text << "    ret\n\n";
    }

    // ---------- Variables ----------

    // Gives a new variable a slot on the stack. Every slot is 8 bytes, which is
    // plenty for an int and keeps everything lined up.
    // Gives a new variable a slot on the stack. Every slot is 8 bytes, which is
    // plenty for an int and keeps everything lined up.
    void allocate(const std::string& name, const std::string& type, int count = 0) {
        if (vars.count(name)) {
            throw std::runtime_error("variable '" + name + "' is already declared");
        }
        // An array takes one slot per element, one after another. 'offset' is the
        // distance of its FIRST element below RBP; the next elements sit at higher
        // addresses. Elements are numbered from 1, so element n is at (RBP - offset + (n-1)*8).
        int slots = (count > 0) ? count : 1;
        localBytes += 8 * slots;
        vars[name] = Variable{type, localBytes, count};
    }

    int newTemp(){
        localBytes += 8;
        return localBytes;
    }

    // Makes a fresh, unique name for a place in the code that we can jump to.
    // Names starting with a '.' belong to the function they are in.
    std::string newLabel() {
        return ".L" + std::to_string(labelCount++);
    }

    // Finds a variable we've already declared.
    const Variable& lookup(const std::string& name) const {
        auto it = vars.find(name);
        if (it == vars.end()) {
            throw std::runtime_error("unknown variable '" + name + "'");
        }
        return it->second;
    }

    // The assembly text for a variable's slot, like "[rbp-8]".
    static std::string slotOf(const Variable& v) {
        return "[rbp-" + std::to_string(v.offset) + "]";
    }

    // The slot of element number 'n' of an array, when 'n' is a known number: "[rbp-24]".
    // Elements are numbered from 1, so element 1 is the FIRST slot.
    static std::string elementSlot(const Variable& arr, int n) {
        return "[rbp-" + std::to_string(arr.offset - (n - 1) * 8) + "]";
    }

    // Works out WHICH element an index expression means, and returns the assembly text
    // for that element's memory, like "[rbp + rax*8 - 48]".
    // The index ends up in RAX, so the caller must use the returned text before
    // anything else changes RAX.
    std::string genElement(const std::string& name, const Node& index) {
        const Variable& arr = lookup(name);
        if (arr.count == 0) {
            throw std::runtime_error("'" + name + "' is not an array");
        }
        genExpr(index);                                  // EAX = the index
        body << "    movsxd rax, eax          ; widen the index to 64 bits\n";
        // The index counts from 1, so index 1 must land on the first slot: the extra
        // "+ 8" in the subtraction is that step back.
        return "[rbp + rax*8 - " + std::to_string(arr.offset + 8) + "]";
    }

    // ---------- Statements ----------

    // dynamic_cast asks "is this node really a TypeStmt?" and gives back a pointer
    // to it if so, or nullptr if not. We try each kind of statement in turn.
    void genStatement(const Node& node)
    {
        if (auto* t = dynamic_cast<const TypeStmt*>(&node))   { genType(*t);   return; }
        if (auto* r = dynamic_cast<const ReturnStmt*>(&node)) { genReturn(*r); return; }
        if (auto* d = dynamic_cast<const VarDecl*>(&node))    { genVarDecl(*d); return; }
        if (auto* a = dynamic_cast<const AssignStmt*>(&node)) { genAssign(*a); return; }
        if (auto* rd = dynamic_cast<const ReadStmt*>(&node))   { genRead(*rd);  return; }
        if (auto* b = dynamic_cast<const Block*>(&node))      { genBlock(*b);  return; }
        if (auto* i = dynamic_cast<const IfStmt*>(&node))     { genIf(*i);     return; }
        if (auto* w = dynamic_cast<const WhileLoop*>(&node))  { genWhile(*w);  return; }
        if (auto* f = dynamic_cast<const ForLoop*>(&node))    { genFor(*f);    return; }
        if (dynamic_cast<const BreakStmt*>(&node))            { genBreak();    return; }
        if (dynamic_cast<const ContinueStmt*>(&node))         { genContinue(); return; }
        if (auto* c = dynamic_cast<const CallExpr*>(&node))    { genCall(*c, false); return;}
        throw std::runtime_error(std::string("this kind of statement is not supported yet: ") +
                         typeid(node).name());
    }

        // int x = 5       int a, b = 7       int c       int nums[5] = [1, 2, 3]
    void genVarDecl(const VarDecl& decl) {
        if (decl.typeName != "int" && decl.typeName != "char" && decl.typeName != "bool") {
            throw std::runtime_error("only int, char and bool variables are supported so far");
        }

        // A single starting value (not a [list]) is worked out ONCE, up front, into EAX.
        // We do this BEFORE declaring the names, so "int x = x + 1" is an error
        // instead of reading a variable that doesn't exist yet.
        const ArrayInit* list = dynamic_cast<const ArrayInit*>(decl.init.get());
        if (decl.init && !list) {
            genExpr(*decl.init);
        }

        for (const Declname& n : decl.names) {
            if (n.size.empty()) {
                // ----- a plain variable -----
                allocate(n.name, decl.typeName);
                // With no starting value, a new variable starts at 0.
                body << "    mov dword " << slotOf(lookup(n.name)) << ", "
                     << (decl.init ? "eax" : "0") << "      ; " << n.name << "\n";
                continue;
            }

            // ----- an array -----
            int count = std::stoi(n.size);
            if (count <= 0) {
                throw std::runtime_error("the array '" + n.name + "' needs a size of at least 1");
            }
            allocate(n.name, decl.typeName, count);
            const Variable& arr = lookup(n.name);

            // Every element starts at 0. A small loop clears them: ECX counts down from
            // 'count' to 1, and each turn clears element number ECX.
            std::string loop = newLabel();
            body << "    mov ecx, " << count << "              ; clear the array " << n.name << "\n";
            body << loop << ":\n";
            body << "    mov dword [rbp + rcx*8 - " << (arr.offset + 8) << "], 0\n";
            body << "    sub ecx, 1\n";
            body << "    jnz " << loop << "\n";

            if (list) {
                // int nums[5] = [10, 20, 30]: element 1 gets 10, element 2 gets 20, ...
                if (static_cast<int>(list->items.size()) > count) {
                    throw std::runtime_error("more starting values than the array '" + n.name +
                                             "' has room for");
                }
                for (size_t i = 0; i < list->items.size(); i++) {
                    genExpr(*list->items[i]);
                    body << "    mov dword " << elementSlot(arr, static_cast<int>(i) + 1)
                         << ", eax      ; " << n.name << "[" << i + 1 << "]\n";
                }
            } else if (decl.init) {
                // A single value, as in "int big[100] <- 5": it goes in the element named
                // in the brackets, here element 100. (The others are already 0 from the loop.)
                body << "    mov dword " << elementSlot(arr, count) << ", eax      ; " << n.name << "[" << count << "]\n";
            }
        }
    }

    // x = 5       x <- x + 1       nums[i] = 7
    // Both arrows do the same thing here.
    void genAssign(const AssignStmt& stmt) {
        const Variable& v = lookup(stmt.name);      // fails if the variable doesn't exist

        if (stmt.index) {
            // An array element. Order matters: computing the index uses EAX, so we
            // first work out the new value and keep it safe in a temporary slot.
            genExpr(*stmt.value);
            int temp = newTemp();
            body << "    mov dword [rbp-" << temp << "], eax      ; save the new value\n";
            std::string target = genElement(stmt.name, *stmt.index);    // RAX = the index
            body << "    mov ecx, dword [rbp-" << temp << "]      ; ECX = the new value again\n";
            body << "    mov dword " << target << ", ecx      ; " << stmt.name << "[...] = ...\n";
        } else {
            if (v.count > 0) {
                throw std::runtime_error("'" + stmt.name + "' is an array: use " + stmt.name + "[index]");
            }
            genExpr(*stmt.value);                   // the new value ends up in EAX
            body << "    mov dword " << slotOf(v) << ", eax      ; " << stmt.name << " = ...\n";
        }
    }

    // { statement statement ... }
    void genBlock(const Block& block) {
        // Variables declared inside the block stop existing at its closing brace.
        // We remember which variables exist now and restore that list afterwards.
        // (Their stack slots stay reserved; they just can't be named any more.)
        auto saved = vars;
        for (const auto& stmt : block.statements) {
            genStatement(*stmt);
        }
        vars = saved;
    }

    // if (condition) { ... } else { ... }
    // The idea: work out the condition; if it is 0 (false), jump over the 'then' part.
    void genIf(const IfStmt& stmt) {
        std::string skip = newLabel();          // where we land when the condition is false

        genExpr(*stmt.condition);               // EAX = 1 (true) or 0 (false)
        body << "    cmp eax, 0\n";
        body << "    je " << skip << "      ; false: skip the 'then' part\n";
        genStatement(*stmt.thenBranch);

        if (stmt.elseBranch) {
            std::string end = newLabel();
            body << "    jmp " << end << "      ; the 'then' part ran, so skip the 'else' part\n";
            body << skip << ":\n";
            genStatement(*stmt.elseBranch);     // a Block, or another IfStmt for "else if"
            body << end << ":\n";
        } else {
            body << skip << ":\n";
        }
    }

        // while (condition) { ... }
    // The idea: test at the top; if false, jump past the loop; at the bottom, jump back up.
    void genWhile(const WhileLoop& stmt) {
        std::string top = newLabel();
        std::string end = newLabel();

        body << top << ":\n";
        genExpr(*stmt.condition);
        body << "    cmp eax, 0\n";
        body << "    je " << end << "      ; false: leave the loop\n";

        loops.push_back({top, end});            // 'continue' goes to the top, 'break' to the end
        genStatement(*stmt.body);
        loops.pop_back();

        body << "    jmp " << top << "      ; go round again\n";
        body << end << ":\n";
    }

    // for (init; condition; update) { ... }
    // The idea: run init once; then repeat: test the condition, run the body, run the update.
    void genFor(const ForLoop& stmt) {
        // A variable declared in the header, as in "for (int i = 0; ...)", belongs to the
        // loop only. We remember the variables that exist now and restore them afterwards.
        auto saved = vars;

        std::string top = newLabel();
        std::string next = newLabel();          // where 'continue' lands: the update step
        std::string end = newLabel();

        if (stmt.init) {
            genStatement(*stmt.init);           // runs once, before the loop starts
        }

        body << top << ":\n";
        if (stmt.condition) {                   // an empty condition means "always true"
            genExpr(*stmt.condition);
            body << "    cmp eax, 0\n";
            body << "    je " << end << "      ; false: leave the loop\n";
        }

        loops.push_back({next, end});
        genStatement(*stmt.body);
        loops.pop_back();

        body << next << ":\n";
        if (stmt.update) {
            genStatement(*stmt.update);
        }
        body << "    jmp " << top << "      ; go round again\n";
        body << end << ":\n";

        vars = saved;
    }

    // break      leaves the nearest loop
    void genBreak() {
        if (loops.empty()) {
            throw std::runtime_error("'break' can only be used inside a loop");
        }
        body << "    jmp " << loops.back().end << "      ; break\n";
    }

    // continue   skips to the next turn of the nearest loop
    void genContinue() {
        if (loops.empty()) {
            return;                             // outside a loop, 'continue' does nothing
        }
        body << "    jmp " << loops.back().next << "      ; continue\n";
    }

    // Read[x]      Read[nums[i]]      reads what the user types into a variable or element
    void genRead(const ReadStmt& stmt) {
        const Variable& v = lookup(stmt.name);
        if (v.type != "int" && v.type != "char") {
            throw std::runtime_error("Read only supports int and char variables so far");
        }

        // 'target' is the assembly text for the memory we read into.
        std::string target;
        if (stmt.index) {
            target = genElement(stmt.name, *stmt.index);     // like "[rbp + rax*8 - 48]"
        } else {
            if (v.count > 0) {
                throw std::runtime_error("'" + stmt.name + "' is an array: use " + stmt.name + "[index]");
            }
            target = slotOf(v);
        }

        // scanf writes only ONE byte for a character, so for a char we clear the whole
        // slot first. That way the other bytes are 0 and the slot holds the
        // character's code as an ordinary number.
        if (v.type == "char") {
            body << "    mov dword " << target << ", 0\n";
        }
        // scanf("%d", &x): RCX = the format, RDX = the ADDRESS to write to.
        body << "    lea rdx, " << target << "      ; the address of " << stmt.name << "\n";
        body << "    lea rcx, [rel " << (v.type == "int" ? "fmt_d" : "fmt_in_c") << "]\n";
        body << "    call scanf\n";
    }

    // Type["Hello" x], \n
    void genType(const TypeStmt& stmt) {
        for (const auto& item : stmt.items) {
            if (auto* s = dynamic_cast<const StringLit*>(item.get())) {
                std::string label = addString(s->value);
                // printf("%s", text): RCX = the format, RDX = the text to print.
                // We always print through "%s" so a '%' inside the text is never
                // mistaken for a format code.
                body << "    lea rcx, [rel fmt_s]\n";
                body << "    lea rdx, [rel " << label << "]\n";
                body << "    call printf\n";
            } else {
                // Any other item is a number: compute it into EAX, then
                // printf("%d", value): RCX = the format, EDX = the number.
                genExpr(*item);
                // A char prints as a character; anything else prints as a number.
                const char* fmt = (exprType(*item) == "char") ? "fmt_c" : "fmt_d";
                body << "    mov edx, eax\n";
                body << "    lea rcx, [rel " << fmt << "]\n";
                body << "    call printf\n";
            }
        }
        if (stmt.newline) {
            body << "    lea rcx, [rel fmt_nl]    ; the \", \\n\" after Type[...]\n";
            body << "    call printf\n";
        }
    }

    // return 0       return x       return   (nothing, for a procedure)
    void genReturn(const ReturnStmt& stmt) {
        bool hasType = !currentFn->returnType.empty();      // declared with "-> type"?
        if (stmt.value) {
            // main always returns an int, so it may return a value even without "->".
            if (!hasType && currentFn->name != "main") {
                throw std::runtime_error("the function '" + currentFn->name +
                                         "' returns nothing, so 'return' can't have a value");
            }
            genExpr(*stmt.value);                   // the return value ends up in EAX, where callers look
        } else {
            if (hasType) {
                throw std::runtime_error("the function '" + currentFn->name + "' must return a value");
            }
            body << "    xor eax, eax\n";
        }
        body << "    jmp .exit\n";
    }

    // ---------- Expressions ----------

    // The type of an expression: "string", "char" or "int". Used to decide how to print it.
    std::string exprType(const Node& node) const {
        if (dynamic_cast<const StringLit*>(&node)) return "string";
        if (dynamic_cast<const CharLit*>(&node))   return "char";
        if (auto* v = dynamic_cast<const NameExpr*>(&node)) return lookup(v->name).type;
        if (auto* ix = dynamic_cast<const IndexExpr*>(&node)) return lookup(ix->name).type;
        if (auto* c = dynamic_cast<const CallExpr*>(&node)) {
            auto it = funcs.find(c->name);
            if (it != funcs.end() && !it->second.returnType.empty()) return it->second.returnType;
        }
        return "int";           // numbers and math
    }

    // Works out the value of an expression and leaves it in EAX.
    void genExpr(const Node& node) {
        if (auto* n = dynamic_cast<const NumberLit*>(&node)) {
            body << "    mov eax, " << n->text << "\n";
            return;
        }
        if (auto* bl = dynamic_cast<const BoolLit*>(&node)) {
            body << "    mov eax, " << (bl->value ? 1 : 0) << "             ; " << (bl->value ? "true" : "false") << "\n";
            return;
        }
        if (auto* c = dynamic_cast<const CharLit*>(&node)) {
            // A character is just its code number: '+' is 43, 'a' is 97, and so on.
            body << "    mov eax, " << static_cast<int>(static_cast<unsigned char>(c->value))
                 << "             ; '" << printable(std::string(1, c->value)) << "'\n";
            return;
        }
        if (auto* c = dynamic_cast<const CallExpr*>(&node)) {
            genCall(*c, true);                      // the result ends up in EAX
            return;
        }
        if (auto* ix = dynamic_cast<const IndexExpr*>(&node)) {
            std::string element = genElement(ix->name, *ix->index);
            body << "    mov eax, dword " << element << "      ; read " << ix->name << "[...]\n";
            return;
        }
        if (auto* v = dynamic_cast<const NameExpr*>(&node)) {
            const Variable& var = lookup(v->name);
            if (var.type != "int" && var.type != "char" && var.type != "bool") {
                throw std::runtime_error("only int, char and bool variables are supported so far");
            }
            body << "    mov eax, dword " << slotOf(var) << "      ; read " << v->name << "\n";
            if (var.count > 0) {
                throw std::runtime_error("'" + v->name + "' is an array: use " + v->name + "[index]");
            }
            return;
        }
        if (auto* b = dynamic_cast<const BinaryExpr*>(&node)){
            genBinary(*b);
            return;
        }
        throw std::runtime_error("this kind of expression is not supported yet");
    }

    // add(a, b)      a call. 'wantValue' is true when the result is used (as in x = add(1, 2)).
    void genCall(const CallExpr& call, bool wantValue) {
        auto it = funcs.find(call.name);
        if (it == funcs.end()) {
            throw std::runtime_error("unknown function '" + call.name + "'");
        }
        const FuncInfo& f = it->second;
        if (call.args.size() != f.params.size()) {
            throw std::runtime_error("the function '" + call.name + "' takes " +
                                     std::to_string(f.params.size()) + " argument(s) but got " +
                                     std::to_string(call.args.size()));
        }
        if (wantValue && f.returnType.empty()) {
            throw std::runtime_error("the function '" + call.name +
                                     "' returns nothing, so it can't be used as a value");
        }

        // Step 1: work out every argument and park it in a temporary slot of its own.
        std::vector<int> temps;
        for (const auto& arg : call.args) {
            genExpr(*arg);
            int temp = newTemp();
            body << "    mov dword [rbp-" << temp << "], eax      ; an argument for " << call.name << "\n";
            temps.push_back(temp);
        }
        // Step 2: with every argument ready, load them into the argument registers.
        for (size_t i = 0; i < temps.size(); i++) {
            body << "    mov " << argRegs[i] << ", dword [rbp-" << temps[i] << "]\n";
        }
        // Step 3: the call. Our frame already keeps RSP aligned and has the 32 bytes
        // of shadow space. The function's result comes back in EAX.
        body << "    call " << funcLabel(call.name) << "\n";
    }

    void genBinary(const BinaryExpr& expr){
        const std::string& op = expr.op;
        // && and || are special: they must NOT work out the right side when the
        // left side already decides the answer, so they get their own function.
        if (op == "&&" || op == "||") {
            genLogic(expr);
            return;
        }
        bool isCompare = (op == "<" || op == ">" || op == "<=" || op == ">=" ||
                          op == "==" || op == "!=");
        if (!isCompare && op != "+" && op != "-" && op != "*" && op != "/" && op != "%") {
            throw std::runtime_error("the operator '" + op + "' is not supported yet");
        }
       genExpr(*expr.left);
        int temp = newTemp();
        body << "    mov dword [rbp-" << temp << "], eax      ; save the left side\n";

        genExpr(*expr.right);
        body << "    mov ecx, eax             ; ECX = the right side\n";
        body << "    mov eax, dword [rbp-" << temp << "]      ; EAX = the left side again\n";

        if (isCompare) {
            // cmp compares EAX with ECX. A set... instruction then puts 1 (true) or
            // 0 (false) in AL, the lowest byte of EAX, and movzx widens it to all of EAX.
            const char* set = op == "<"  ? "setl"  : op == ">"  ? "setg"  :
                              op == "<=" ? "setle" : op == ">=" ? "setge" :
                              op == "==" ? "sete"  : "setne";
            body << "    cmp eax, ecx\n";
            body << "    " << set << " al\n";
            body << "    movzx eax, al\n";
            return;
        }

        if (op == "+")      body << "    add eax, ecx\n";
        else if (op == "-") body << "    sub eax, ecx\n";
        else if (op == "*") body << "    imul eax, ecx\n";
        else {

            body << "    cdq\n";
            body << "    idiv ecx\n";
            if (op == "%") body << "    mov eax, edx             ; for %, the answer is the remainder\n";
        }
    }

    // left && right      left || right
    // Short-circuit: if the left side already decides the answer, the right side is never run.
    void genLogic(const BinaryExpr& expr) {
        bool isAnd = (expr.op == "&&");
        std::string stop = newLabel();      // where we jump when a side decides the answer
        std::string end = newLabel();
        const char* jumpIfDecided = isAnd ? "je " : "jne ";    // && stops on false, || stops on true

        genExpr(*expr.left);
        body << "    cmp eax, 0\n";
        body << "    " << jumpIfDecided << stop << "\n";

        genExpr(*expr.right);
        body << "    cmp eax, 0\n";
        body << "    " << jumpIfDecided << stop << "\n";

        // Neither side decided it, so && is true and || is false.
        body << "    mov eax, " << (isAnd ? 1 : 0) << "\n";
        body << "    jmp " << end << "\n";
        body << stop << ":\n";
        // A side decided it, so && is false and || is true.
        body << "    mov eax, " << (isAnd ? 0 : 1) << "\n";
        body << end << ":\n";
    }

    // ---------- Helpers ----------

    // Stores a string in the .data section and returns the name we gave it.
    // The characters are written as plain numbers (their codes), so quotes,
    // newlines and anything else inside the text can never break the assembly file.
    std::string addString(const std::string& value) {
        std::string label = "str" + std::to_string(stringCount++);
        data << label << " db ";
        for (unsigned char c : value) {
            data << static_cast<int>(c) << ", ";
        }
        data << "0                  ; \"" << printable(value) << "\"\n";   // the 0 ends the string
        return label;
    }

    // A copy of the text that is safe to put inside an assembly comment.
    static std::string printable(const std::string& s) {
        std::string out;
        for (unsigned char c : s) {
            out += (c >= 32 && c < 127) ? static_cast<char>(c) : '.';
        }
        return out;
    }
};