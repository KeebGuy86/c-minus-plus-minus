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
                const std::string& rt = fn->returnType;
                if (!rt.empty() && rt != "int" && rt != "char" && rt != "bool" && rt != "string") {
                    throw std::runtime_error("the function '" + fn->name +
                                             "' has a return type that is not supported yet: " + rt);
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
        out << "extern scanf                 ; scanf reads what the user types\n";
        out << "extern strncpy               ; strncpy copies text\n";
        out << "extern strcmp                ; strcmp compares two pieces of text\n\n";
        out << "section .data\n";
        out << "fmt_s  db \"%s\", 0            ; printf format: print a string\n";
        out << "fmt_d  db \"%d\", 0            ; printf format: print an int\n";
        out << "fmt_nl db 10, 0              ; printf format: just a newline (10)\n";
        out << "fmt_c  db \"%c\", 0            ; printf format: print a character\n";
        out << "fmt_in_c db \" %c\", 0         ; scanf format: read a character (the space skips a leftover Enter)\n";
        out << "fmt_in_s db \"%255s\", 0       ; scanf format: read one word of up to 255 characters\n";
        out << data.str();
        out << "\nsection .bss\n";
        out << "retbuf resb 256              ; where a function leaves the string it returns\n";
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
    static constexpr const char* argRegs64[4] = {"rcx", "rdx", "r8", "r9"};

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

        // PHASE 1: save every incoming register straight away. A number arrives in 32 bit register
        std::vector<int> pointerTemps(fn.params.size(), 0);
        for (size_t i = 0; i < fn.params.size(); i++) {
            const Param& p = fn.params[i];
            if (p.typeName != "int" && p.typeName != "char" && p.typeName != "bool" &&
                p.typeName != "string") {
                throw std::runtime_error("only int, char, bool and string parameters are supported so far");
            }
            if (p.typeName == "string") {
                pointerTemps[i] = newTemp();
                body << "    mov qword [rbp-" << pointerTemps[i] << "], " << argRegs64[i]
                     << "      ; where the caller's text for " << p.name << " is\n";
            } else {
                allocate(p.name, p.typeName);
                body << "    mov dword " << slotOf(lookup(p.name)) << ", " << argRegs[i]
                     << "      ; parameter " << p.name << "\n";
            }
        }

        // PHASE 2: give each string parameter a buffer of its own and copy the caller's
        // text into it. A string is always COPIED, so changing the parameter inside the
        // function can never change the caller's string.
        for (size_t i = 0; i < fn.params.size(); i++) {
            const Param& p = fn.params[i];
            if (p.typeName != "string") continue;
            allocate(p.name, "string");
            const Variable& v = lookup(p.name);
            body << "    mov rdx, qword [rbp-" << pointerTemps[i] << "]\n";
            body << "    lea rcx, " << slotOf(v) << "\n";
            body << "    mov r8d, 255\n";
            body << "    call strncpy\n";
            body << "    mov byte [rbp-" << (v.offset - 255) << "], 0\n";
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
        if (frame > 1000000) {
            throw std::runtime_error("the function '" + fn.name + "' needs too much stack space");
        }

        text << funcLabel(fn.name) << ":\n";
        text << "    push rbp                 ; save the caller's frame pointer\n";
        text << "    mov rbp, rsp             ; our frame starts here\n";
        // Windows grows the stack one 4096-byte page at a time, and a program must touch
        // each new page in order. So a big frame is reserved one page at a time, touching
        // each page as we go. (A small frame is just one 'sub'.)
        for (int page = 0; page < frame / 4096; page++) {
            text << "    sub rsp, 4096\n";
            text << "    mov qword [rsp], 0       ; touch the new stack page\n";
        }
        if (frame % 4096 > 0) {
            text << "    sub rsp, " << (frame % 4096) << "              ; room for our variables and for the calls we make\n";
        }
        text << body.str();

        // Falling off the end of a function returns a default: 0, or an empty string.
        // A 'return' statement jumps straight to .exit with its own value already set.
        if (fn.returnType == "string") {
            text << "    mov byte [rel retbuf], 0 ; default return value: an empty string\n";
            text << "    lea rax, [rel retbuf]\n";
        } else {
            text << "    xor eax, eax             ; default return value: 0\n";
        }
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
        localBytes += (type == "string") ? 256 * slots : 8 * slots;
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
    static std::string elementSlot(const Variable& arr, int n) {
        return "[rbp-" + std::to_string(elementOffset(arr, n)) + "]";
    }

    // How far below RBP element number 'n' starts. Elements are 8 bytes each,
    // except in an array of strings, where each element is a 256-byte buffer.
    static int elementOffset(const Variable& arr, int n) {
        int stride = (arr.type == "string") ? 256 : 8;
        return arr.offset - (n - 1) * stride;
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
        if (arr.type == "string"){
            throw std::runtime_error("'" + name + "' holds strings, which can't be used as numbers");
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
        if (decl.typeName == "string") {
            genStringDecl(decl);
            return;
        }
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

        if (v.type == "string") {
            if (exprType(*stmt.value) != "string") {
                throw std::runtime_error("a string can only be given text or another string");
            }
            if (stmt.index) {
                genStringCopyToElement(stmt.name, *stmt.index, *stmt.value);
            } else {
                if (v.count > 0) {
                    throw std::runtime_error("'" + stmt.name + "' is an array: use " + stmt.name + "[index]");
                }
                // "s = s" would copy a string onto itself, which strncpy does not allow,
                // so there is nothing to do.
                auto* other = dynamic_cast<const NameExpr*>(stmt.value.get());
                if (other && other->name == stmt.name) return;
                genStringCopyAt(v.offset, *stmt.value);
            }
            return;
        }

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
        if (v.type == "string") {
            // scanf("%255s", buffer): reads ONE word (up to a space or Enter), at most 255 characters.
            if (stmt.index) {
                genStringElementAddress(stmt.name, *stmt.index);   // RAX = that element's buffer
                body << "    mov byte [rax], 0        ; in case nothing is typed\n";
                body << "    mov rdx, rax\n";
            } else {
                if (v.count > 0) {
                    throw std::runtime_error("'" + stmt.name + "' is an array: use " + stmt.name + "[index]");
                }
                body << "    mov byte " << slotOf(v) << ", 0      ; in case nothing is typed\n";
                body << "    lea rdx, " << slotOf(v) << "      ; the address of " << stmt.name << "\n";
            }
            body << "    lea rcx, [rel fmt_in_s]\n";
            body << "    call scanf\n";
            return;
        }
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
            if (exprType(*item) == "string") {
                genStringAddress(*item);            // RAX = where the characters are
                // printf("%s", text): RCX = the format, RDX = the text to print.
                // We always print through "%s" so a '%' inside the text is never
                // mistaken for a format code.
                body << "    mov rdx, rax\n";
                body << "    lea rcx, [rel fmt_s]\n";
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
            if (currentFn->returnType == "string") {
                if (exprType(*stmt.value) != "string") {
                    throw std::runtime_error("the function '" + currentFn->name +
                                             "' returns a string, so 'return' needs text or a string");
                }
                genStringAddress(*stmt.value);      // RAX = the characters to return
                // This function's own buffers disappear when it ends, so the text is
                // copied to one shared buffer, 'retbuf', where the caller picks it up.
                body << "    mov rdx, rax\n";
                body << "    lea rcx, [rel retbuf]\n";
                body << "    mov r8d, 255\n";
                body << "    call strncpy\n";
                body << "    mov byte [rel retbuf + 255], 0\n";
                body << "    lea rax, [rel retbuf]      ; the caller reads the result from here\n";
            } else {
                if (exprType(*stmt.value) == "string") {
                    throw std::runtime_error("the function '" + currentFn->name +
                                             "' doesn't return a string");
                }
                genExpr(*stmt.value);               // the return value ends up in EAX, where callers look
            }
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
            auto fit = funcs.find(c->name);
            if (fit != funcs.end() && fit->second.returnType == "string"){
                throw std::runtime_error("the function '" + c->name + "' returns a string, which cant be used as a number.");
            }
            genCall(*c, true);
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

       //string is noted as the address of its location
        std::vector<int> temps;
        for (size_t i = 0; i < call.args.size(); i++) {
            bool wantsString = (f.params[i].typeName == "string");
            bool isString = (exprType(*call.args[i]) == "string");
            if (wantsString != isString) {
                throw std::runtime_error("argument " + std::to_string(i + 1) + " of '" + call.name +
                                         "' should be " + (wantsString ? "a string" : "a number"));
            }
            if (wantsString) {
                genStringAddress(*call.args[i]);
                int temp = newTemp();
                body << "    mov qword [rbp-" << temp << "], rax      ; an argument for " << call.name << "\n";
                temps.push_back(temp);
            } else {
                genExpr(*call.args[i]);
                int temp = newTemp();
                body << "    mov dword [rbp-" << temp << "], eax      ; an argument for " << call.name << "\n";
                temps.push_back(temp);
            }
        }
        // load arguments in their registers
        for (size_t i = 0; i < temps.size(); i++) {
            if (f.params[i].typeName == "string") {
                body << "    mov " << argRegs64[i] << ", qword [rbp-" << temps[i] << "]\n";
            } else {
                body << "    mov " << argRegs[i] << ", dword [rbp-" << temps[i] << "]\n";
            }
        }
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
        if (exprType(*expr.left) == "string" || exprType(*expr.right) == "string") {
            if ((op != "==" && op != "!=") || exprType(*expr.left) != exprType(*expr.right)) {
                throw std::runtime_error("strings can only be compared with == and !=, and only with other strings");
                }
            genStringCompare(expr);
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

    // ---------- Strings ----------
    // string s     string s = "hi"     string a, b = "x"     string names[3] = ["a", "b"]
    void genStringDecl(const VarDecl& decl) {
        const ArrayInit* list = dynamic_cast<const ArrayInit*>(decl.init.get());
        if (list) {
            for (const auto& item : list->items) {
                if (exprType(*item) != "string") {
                    throw std::runtime_error("an array of strings can only be started with text");
                }
            }
        } else if (decl.init && exprType(*decl.init) != "string") {
            throw std::runtime_error("a string can only be started with text or another string");
        }

        for (const Declname& n : decl.names) {
            if (n.size.empty()) {
                // ----- a plain string -----
                allocate(n.name, "string");
                const Variable& v = lookup(n.name);
                if (decl.init) {
                    genStringCopyAt(v.offset, *decl.init);
                } else {
                    body << "    mov byte " << slotOf(v) << ", 0      ; " << n.name << " starts empty\n";
                }
                continue;
            }

            // ----- an array of strings -----
            int count = std::stoi(n.size);
            if (count <= 0) {
                throw std::runtime_error("the array '" + n.name + "' needs a size of at least 1");
            }
            allocate(n.name, "string", count);
            const Variable& arr = lookup(n.name);

            // Every element starts empty: set the first byte of each buffer to 0.
            // ECX counts down from 'count' to 1; element ECX starts (ECX-1)*256 bytes in.
            std::string loop = newLabel();
            body << "    mov ecx, " << count << "              ; start every element of " << n.name << " empty\n";
            body << loop << ":\n";
            body << "    mov rax, rcx\n";
            body << "    sub rax, 1\n";
            body << "    shl rax, 8               ; times 256, the size of one buffer\n";
            body << "    mov byte [rbp + rax - " << arr.offset << "], 0\n";
            body << "    sub ecx, 1\n";
            body << "    jnz " << loop << "\n";

            if (list) {
                if (static_cast<int>(list->items.size()) > count) {
                    throw std::runtime_error("more starting values than the array '" + n.name +
                                             "' has room for");
                }
                for (size_t i = 0; i < list->items.size(); i++) {
                    genStringCopyAt(elementOffset(arr, static_cast<int>(i) + 1), *list->items[i]);
                }
            } else if (decl.init) {
                // A single value goes in the element named in the brackets.
                genStringCopyAt(elementOffset(arr, count), *decl.init);
            }
        }
    }

    // Leaves the ADDRESS of a string's characters in RAX. Works for a string literal
    // ("hello"), a string variable, an element of a string array, and a call that
    // returns a string.
    void genStringAddress(const Node& node) {
        if (auto* s = dynamic_cast<const StringLit*>(&node)) {
            body << "    lea rax, [rel " << addString(s->value) << "]\n";
            return;
        }
        if (auto* v = dynamic_cast<const NameExpr*>(&node)) {
            const Variable& var = lookup(v->name);
            if (var.type == "string") {
                if (var.count > 0) {
                    throw std::runtime_error("'" + v->name + "' is an array: use " + v->name + "[index]");
                }
                body << "    lea rax, " << slotOf(var) << "      ; the address of " << v->name << "\n";
                return;
            }
        }
        if (auto* ix = dynamic_cast<const IndexExpr*>(&node)) {
            genStringElementAddress(ix->name, *ix->index);
            return;
        }
        if (auto* c = dynamic_cast<const CallExpr*>(&node)) {
            auto it = funcs.find(c->name);
            if (it != funcs.end() && it->second.returnType == "string") {
                genCall(*c, true);                  // RAX = the shared 'retbuf'
                // 'retbuf' is overwritten by the next string-returning call, so copy the
                // text into a buffer of our own right away. (That is what makes
                // f("a") == f("b") work: each call gets its own copy.)
                int buf = newBuffer();
                body << "    mov rdx, rax\n";
                body << "    lea rcx, [rbp-" << buf << "]\n";
                body << "    mov r8d, 255\n";
                body << "    call strncpy\n";
                body << "    mov byte [rbp-" << (buf - 255) << "], 0\n";
                body << "    lea rax, [rbp-" << buf << "]      ; the copy of the returned text\n";
                return;
            }
        }
        throw std::runtime_error("this kind of string expression is not supported yet");
    }

    // RAX = the address of element 'index' of an array of strings.
    // Elements are numbered from 1, and each one is a 256-byte buffer.
    void genStringElementAddress(const std::string& name, const Node& index) {
        const Variable& arr = lookup(name);
        if (arr.count == 0 || arr.type != "string") {
            throw std::runtime_error("'" + name + "' is not an array of strings");
        }
        genExpr(index);                              // EAX = the index
        body << "    movsxd rax, eax          ; widen the index to 64 bits\n";
        body << "    sub rax, 1               ; elements are numbered from 1\n";
        body << "    shl rax, 8               ; times 256, the size of one buffer\n";
        body << "    lea rax, [rbp + rax - " << arr.offset << "]\n";
    }

    // Copies a string into the buffer that starts 'destOffset' bytes below RBP.
    //max characters is 255
    void genStringCopyAt(int destOffset, const Node& source) {
        genStringAddress(source);                    // RAX = the characters to copy
        body << "    mov rdx, rax             ; 2nd argument: where to copy from\n";
        body << "    lea rcx, [rbp-" << destOffset << "]      ; 1st argument: where to copy to\n";
        body << "    mov r8d, 255             ; 3rd argument: at most this many characters\n";
        body << "    call strncpy\n";
        body << "    mov byte [rbp-" << (destOffset - 255) << "], 0      ; make sure the text is ended\n";
    }

    // names[i] = source: the same copy, but the destination is worked out while running.
    void genStringCopyToElement(const std::string& name, const Node& index, const Node& source) {
        genStringAddress(source);
        int srcTemp = newTemp();
        body << "    mov qword [rbp-" << srcTemp << "], rax      ; save where to copy from\n";
        genStringElementAddress(name, index);        // RAX = the element's buffer
        int dstTemp = newTemp();
        body << "    mov qword [rbp-" << dstTemp << "], rax      ; save where to copy to\n";
        body << "    mov rcx, rax\n";
        body << "    mov rdx, qword [rbp-" << srcTemp << "]\n";
        body << "    mov r8d, 255\n";
        body << "    call strncpy\n";
        body << "    mov rax, qword [rbp-" << dstTemp << "]\n";
        body << "    mov byte [rax + 255], 0      ; make sure the text is ended\n";
    }

    // left == right      left != right      (both are strings)
    void genStringCompare(const BinaryExpr& expr) {
        genStringAddress(*expr.left);
        int temp = newTemp();
        body << "    mov qword [rbp-" << temp << "], rax      ; save the left string's address\n";
        genStringAddress(*expr.right);
        body << "    mov rdx, rax\n";
        body << "    mov rcx, qword [rbp-" << temp << "]\n";
        body << "    call strcmp                ; EAX = 0 when the two are equal\n";
        body << "    cmp eax, 0\n";
        body << "    " << (expr.op == "==" ? "sete" : "setne") << " al\n";
        body << "    movzx eax, al\n";
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

    int newBuffer() {
        localBytes += 256;
        return localBytes;
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