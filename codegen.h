#pragma once

#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
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
        // Only functions produce code. An import needs no instructions for now,
        // because printing comes from the C library (printf).
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
        int offset;             // how many bytes below RBP its slot starts (slot is at [rbp-offset])
    };
    std::map<std::string, Variable> vars;   // name -> variable, for the current function
    int localBytes = 0;                     // how much stack the variables use so far

    // ---------- Functions ----------

    void genFunction(const FunctionDecl& fn) {
        // Each function starts with no variables and an empty body.
        vars.clear();
        localBytes = 0;
        body.str("");
        body.clear();

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

        text << fn.name << ":\n";
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
    void allocate(const std::string& name, const std::string& type) {
        if (vars.count(name)) {
            throw std::runtime_error("variable '" + name + "' is already declared");
        }
        localBytes += 8;
        vars[name] = Variable{type, localBytes};
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
        throw std::runtime_error("this kind of statement is not supported yet");
    }

    // int x = 5       int a, b = 7       int c
    void genVarDecl(const VarDecl& decl) {
        if (decl.typeName != "int" && decl.typeName != "char" && decl.typeName != "bool") {
            throw std::runtime_error("only int, char and bool variables are supported so far");
        }
        for (const Declname& n : decl.names) {
            if (!n.size.empty()) throw std::runtime_error("arrays are not supported yet");
        }
        if (dynamic_cast<const ArrayInit*>(decl.init.get())) {
            throw std::runtime_error("arrays are not supported yet");
        }

        if (decl.init) {
            genExpr(*decl.init);
        }

        for (const Declname& n : decl.names) {
            allocate(n.name, decl.typeName);
            // With no starting value, a new variable starts at 0.
            body << "    mov dword " << slotOf(lookup(n.name)) << ", "
                 << (decl.init ? "eax" : "0") << "      ; " << n.name << "\n";
        }
    }

    // x = 5       x <- x + 1
    // Both arrows do the same thing here.
    void genAssign(const AssignStmt& stmt) {
        if (stmt.index) {
            throw std::runtime_error("array elements are not supported yet");
        }
        const Variable& v = lookup(stmt.name);      // fails if the variable doesn't exist
        genExpr(*stmt.value);                       // the new value ends up in EAX
        body << "    mov dword " << slotOf(v) << ", eax      ; " << stmt.name << " = ...\n";
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
        genStatement(*stmt.body);
        body << "    jmp " << top << "      ; go round again\n";
        body << end << ":\n";
    }

    // Read[x]      reads what the user types into the variable x
    void genRead(const ReadStmt& stmt) {
        if (stmt.index) {
            throw std::runtime_error("array elements are not supported yet");
        }
        const Variable& v = lookup(stmt.name);

        if (v.type == "int") {
            // scanf("%d", &x): RCX = the format, RDX = the ADDRESS of x's slot.
            // 'lea' works out an address without reading what is stored there.
            body << "    lea rdx, " << slotOf(v) << "      ; the address of " << stmt.name << "\n";
            body << "    lea rcx, [rel fmt_d]\n";
            body << "    call scanf\n";
        } else if (v.type == "char") {

            body << "    mov dword " << slotOf(v) << ", 0\n";
            body << "    lea rdx, " << slotOf(v) << "      ; the address of " << stmt.name << "\n";
            body << "    lea rcx, [rel fmt_in_c]\n";
            body << "    call scanf\n";
        } else {
            throw std::runtime_error("Read only supports int and char variables so far");
        }
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

    // return 0       return x
    void genReturn(const ReturnStmt& stmt) {
        if (stmt.value) {
            genExpr(*stmt.value);                   // the return value ends up in EAX, where callers look
        } else {
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
        if (auto* v = dynamic_cast<const NameExpr*>(&node)) {
            const Variable& var = lookup(v->name);
            if (var.type != "int" && var.type != "char" && var.type != "bool") {
                throw std::runtime_error("only int, char and bool variables are supported so far");
            }
            body << "    mov eax, dword " << slotOf(var) << "      ; read " << v->name << "\n";
            return;
        }
        if (auto* b = dynamic_cast<const BinaryExpr*>(&node)){
            genBinary(*b);
            return;
        }
        throw std::runtime_error("this kind of expression is not supported yet");
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