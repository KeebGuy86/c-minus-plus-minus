#pragma once

#include <iostream>
#include <memory>       // std::unique_ptr
#include <string>
#include <vector>

// Base class for every node in the tree.
// 'virtual' means each node type supplies its own version of the function.
struct Node {
    // A virtual destructor makes sure deleting a node through a base-class
    // pointer cleans up the real (derived) object too.
    virtual ~Node() = default;

    // Each node knows how to print itself. 'indent' is how deep it is in the tree.
    // "= 0" means "pure virtual": this class can't be used directly, only derived from.
    virtual void print(int indent) const = 0;
};

// A node owns its children through unique_ptr: one owner per child, and the child is
// freed automatically when its owner is destroyed. This is the same "scope-based
// cleanup" model you chose for the language, with no garbage collector involved.
using NodePtr = std::unique_ptr<Node>;

// Prints two spaces per level of depth. Used by the print() functions.
inline void pad(int indent) {
    std::cout << std::string(indent * 2, ' ');
}

// ---------- Expressions: things that have a value ----------

struct StringLit : Node {
    std::string value;
    explicit StringLit(std::string v) : value(std::move(v)) {}
    void print(int indent) const override {
        pad(indent); std::cout << "String \"" << value << "\"\n";
    }
};

struct NumberLit : Node {
    std::string text;
    explicit NumberLit(std::string t) : text(std::move(t)) {}
    void print(int indent) const override {
        pad(indent); std::cout << "Number " << text << "\n";
    }
};

struct BoolLit : Node {
    bool value;
    explicit BoolLit(bool v) : value(v) {}
    void print(int indent) const override{
        pad(indent); std::cout << "Boolean " << (value ? "true" : "false") << "\n";
    }
};

struct CharLit : Node {
    char value;
    explicit CharLit(char v) : value(v) {}
    void print(int indent) const override{
        pad(indent); std::cout << "Char " << value << "\n";
    }
};

struct NameExpr : Node {
    std::string name;
    explicit NameExpr(std::string n) : name(std::move(n)) {}
    void print(int indent) const override {
        pad(indent); std::cout << "Name " << name << "\n";
    }
};

struct BinaryExpr : Node {
    std::string op;             // "+", "-", "*", "/" or "%"
    NodePtr left, right;        // the two sides, each of which can be any expression
    BinaryExpr(std::string o, NodePtr l, NodePtr r)
        : op(std::move(o)), left(std::move(l)), right(std::move(r)) {}
    void print(int indent) const override {
        pad(indent); std::cout << "Binary " << op << "\n";
        left->print(indent + 1);
        right->print(indent + 1);
    }
};

struct CallExpr : Node {
    std::string name;
    std::vector<NodePtr> args;
    explicit CallExpr(std::string n) : name(std::move(n)) {}
    void print(int indent) const override{
        pad(indent); std::cout << "Call " << name << "\n";
        for (const auto& a : args) a->print(indent + 1);
    }
};

struct IndexExpr : Node {
    std::string name;
    NodePtr index;
    IndexExpr(std::string n, NodePtr i) : name(std::move(n)), index(std::move(i)) {}
    void print(int indent) const override {
        pad(indent); std::cout << "Index " << name << "\n";
        index->print(indent + 1);
    }
};

struct TernaryExpr : Node {
    NodePtr condition, thenValue, elseValue;
    TernaryExpr(NodePtr cond, NodePtr t, NodePtr e)
        : condition(std::move(cond)), thenValue(std::move(t)), elseValue(std::move(e)) {}
    void print(int indent) const override{
        pad(indent); std::cout << "Ternary\n";
        condition->print(indent + 1);
        thenValue->print(indent + 1);
        elseValue->print(indent + 1);
    }
};

// ---------- Statements: things that do something ----------

struct TypeStmt : Node {
    std::vector<NodePtr> items;     // what goes inside the [ ]
    bool newline = false;           // true if ", \n" was written after the ']'
    void print(int indent) const override {
        pad(indent); std::cout << "Type" << (newline ? " (newline)" : "") << "\n";
        for (const auto& item : items) item->print(indent + 1);
    }
};

struct ReturnStmt : Node {
    NodePtr value;                  // the expression being returned
    explicit ReturnStmt(NodePtr v) : value(std::move(v)) {}
    void print(int indent) const override {
        pad(indent); std::cout << "Return\n";
        if (value) value->print(indent + 1);
    }
};

struct Declname{
    std::string name;
    std::string size;
};

struct VarDecl : Node {
    std::string typeName;   //"Integer" "String"
    std::vector<Declname> names;       //variable names
    NodePtr init;           //the strings value, stays empty if nothing was written
    VarDecl(std::string t, std::vector<Declname> ns, NodePtr i)
        : typeName(std::move(t)), names(std::move(ns)), init(std::move(i)) {}
    void print(int indent) const override{
        pad(indent); std::cout << "VarDecl " << typeName << " ";
        for (size_t i = 0; i < names.size(); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << names[i].name;
            if (!names[i].size.empty()) std::cout << "[" << names[i].size << "]";
        }
        std::cout << "\n";
        if (init) init->print(indent + 1);      //only prints a value if there is one
    }
};

struct AssignStmt : Node {
    std::string name;
    std::string op;
    NodePtr value;
    NodePtr index;
    AssignStmt(std::string n, std::string o, NodePtr v, NodePtr idx)
        : name(std::move(n)), op(std::move(o)), value(std::move(v)), index(std::move(idx)) {}
    void print(int indent) const override{
        if (index){
            pad(indent + 1); std::cout << "Index\n";
            index->print(indent + 2);
        }
       pad(indent); std::cout << "Assign: " << name << " (" << op << ")\n";
        value->print(indent + 1);
    }
};

struct ReadStmt : Node {
    std::string name;
    NodePtr index;
    ReadStmt(std::string n, NodePtr idx) : name(std::move(n)), index(std::move(idx)) {}
    void print(int indent) const override{
        if (index){
            pad(indent + 1); std::cout << "Index\n";
            index->print(indent + 2);
        }
        pad(indent); std::cout << "Read " << name << "\n";
    }
};

struct Block : Node {
    std::vector<NodePtr> statements;
    void print(int indent) const override{
        pad(indent); std::cout << "Block\n";
        for (const auto& s : statements) s->print(indent + 1);
    }
};

struct ArrayInit : Node {
    std::vector<NodePtr> items;
    void print(int indent) const override {
        pad(indent); std::cout << "ArrayValues\n";
        for (const auto& i : items) i->print(indent + 1);
    }
};

struct IfStmt : Node {
    NodePtr condition, thenBranch, elseBranch;
    IfStmt(NodePtr c, NodePtr t, NodePtr e)
        : condition(std::move(c)), thenBranch(std::move(t)), elseBranch(std::move(e)) {}
    void print(int indent) const override{
        pad(indent); std::cout << "If\n";
        // Each part gets a label line, then its content one level deeper.
        pad(indent + 1); std::cout << "Condition\n";
        condition->print(indent + 2);
        pad(indent + 1); std::cout << "Then\n";
        thenBranch->print(indent + 2);
        if (elseBranch) {
            pad(indent + 1); std::cout << "Else\n";
            elseBranch->print(indent + 2);
        }       // closes the 'if (elseBranch)'
    }           // closes print()
};

struct WhileLoop : Node {
    NodePtr condition, body;
    WhileLoop(NodePtr cond, NodePtr b) : condition(std::move(cond)), body(std::move(b)) {}
    void print(int indent) const override{
        pad(indent); std::cout << "While\n";
        pad(indent + 1); std::cout << "Condition\n";
        condition->print(indent + 2);
        pad(indent + 1); std::cout << "Body\n";
        body->print(indent + 2);
    }
};

struct ForLoop : Node {
    NodePtr init, condition, update, body;
    ForLoop(NodePtr i, NodePtr cond, NodePtr upd, NodePtr b) : init(std::move(i)), condition(std::move(cond)), update(std::move(upd)), body(std::move(b)) {}
    void print(int indent) const override{
        pad(indent); std::cout << "For\n";
        //only print the parts that were actually written
        if (init) { pad(indent + 1); std::cout << "Init\n"; init->print(indent + 2);}
        if (condition) { pad(indent + 1); std::cout << "Condition\n"; condition->print(indent + 2);}
        if (update) { pad(indent + 1); std::cout << "update\n"; update->print(indent + 2);}

        pad(indent + 1); std::cout << "Body\n";
        body->print(indent + 2);
    }
};

struct BreakStmt : Node {       //break
    void print(int indent) const override{
        pad(indent); std::cout << "Break\n";
    }
};

struct ContinueStmt: Node {     //continue
    void print(int indent) const override{
        pad(indent); std::cout << "Continue\n";
    }
};

struct CaseClause : Node {
    std::vector<NodePtr> labels;
    bool isDefault = false;
    NodePtr body;
    void print(int indent) const override{
        pad(indent); std::cout << (isDefault ? "Default" : "Case") << "\n";
        for (const auto& l : labels){
            pad(indent + 1); std::cout << "Label\n";
            l->print(indent + 2);
        }
        pad(indent + 1); std::cout << "Body\n";
        body->print(indent + 2);
    }
};

struct SwitchStmt : Node {
    NodePtr subject;
    std::vector<NodePtr> cases;
    explicit SwitchStmt(NodePtr s) : subject(std::move(s)) {}
    void print(int indent) const override {
        pad(indent); std::cout << "Switch\n";
        pad(indent + 1); std::cout << "Subject\n";
        subject->print(indent + 2);
        for (const auto& c : cases) c->print(indent + 1);
    }
};

// ---------- Top-level declarations ----------

struct ImportDecl : Node {
    std::string module;
    explicit ImportDecl(std::string m) : module(std::move(m)) {}
    void print(int indent) const override {
        pad(indent); std::cout << "Import " << module << "\n";
    }
};

struct Param {
    std::string typeName;
    std::string name;
};

struct FunctionDecl : Node {
    std::string name;
    std::vector<Param> params;
    std::string returnType;
    std::vector<NodePtr> body;
    explicit FunctionDecl(std::string n) : name(std::move(n)) {}
    void print(int indent) const override {
        pad(indent); std::cout << "Function " << name << "(";
        for (size_t i = 0; i < params.size(); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << params[i].typeName << " " << params[i].name;
        }
        std::cout << ")";
        if (!returnType.empty()) std::cout << " -> " << returnType;
        std::cout << "\n";
        for (const auto& stmt : body) stmt->print(indent + 1);
    }
};

// The whole program: a list of imports and functions.
struct Program : Node {
    std::vector<NodePtr> items;
    void print(int indent) const override {
        pad(indent); std::cout << "Program\n";
        for (const auto& item : items) item->print(indent + 1);
    }
};