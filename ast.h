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

// Type[ ... ], \n   ->  the items to print, and whether a newline follows
struct TypeStmt : Node {
    std::vector<NodePtr> items;     // what goes inside the [ ]
    bool newline = false;           // true if ", \n" was written after the ]
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
        value->print(indent + 1);
    }
};

struct VarDecl : Node {
    std::string typeName;   //"Integer" "String"
    std::string name;       //variable name
    NodePtr init;           //the strings value, stays empty if nothing was written
    VarDecl(std::string t, std::string n, NodePtr i)
        : typeName(std::move(t)), name(std::move(n)), init(std::move(i)) {}
    void print(int indent) const override{
        pad(indent); std::cout << "VarDecl " << typeName << " " << name << "\n";
        if (init) init->print(indent + 1);      //only prints a value if there is one
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

struct FunctionDecl : Node {
    std::string name;
    std::vector<NodePtr> body;      // the statements inside the { }
    explicit FunctionDecl(std::string n) : name(std::move(n)) {}
    void print(int indent) const override {
        pad(indent); std::cout << "Function " << name << "\n";
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