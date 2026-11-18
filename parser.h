#pragma once

#include <memory>
#include <stdexcept>    // std::runtime_error, used to report syntax errors
#include <string>
#include <vector>
#include "ast.h"
#include "token.h"

class Parser
{
public:
    explicit Parser(std::vector<Token> toks) : tokens(std::move(toks)){}

    std::unique_ptr<Program> parseProgram(){
        auto prog = std::make_unique<Program>(); //Program means, a series of top-level imports and functions
        //until we run out of tokens
        while (!check(TokenKind::EndOfFile)){
            prog->items.push_back(parseTopLevel());
        }
        return  prog;
    }
private:
    std::vector<Token> tokens;
    size_t pos = 0;
    int loopDepth = 0;

    //helpers for moving through the tokens
    //the token under the finger, without moving it
    const Token& current() const{
        return tokens[pos];
    }

    //Takes the current token and moves in, but never passes EndofFile
    const Token& advance(){
       const Token& t = tokens[pos];
       if (t.kind != TokenKind::EndOfFile){
            pos++;
       }
        return t;
    }

    //is the current token of this kind
    [[nodiscard]] bool check(TokenKind kind) const {
        return current().kind == kind;
    }

    //if the current token is this kind, take it and say yes, otherwise leave it and say no
    bool match(TokenKind kind){
        if (check(kind)){
            advance();
            return true;
        }
        return false;
    }

    bool callFollows() const{
        if (!check(TokenKind::Identifier) || pos + 1 >= tokens.size()) return false;
        const Token& name = tokens[pos];
        const Token& next = tokens[pos + 1];
        return next.kind == TokenKind::LParen && next.line == name.line && next.column == name.column + static_cast<int>(name.text.size());
    }

    bool lastWasSemicolon() const {
        return pos > 0 && tokens[pos - 1].kind == TokenKind::Semicolon;
    }

    // Is the current token one of the type names?
    bool isTypeKeyword() const{
        return check(TokenKind::KwInt) || check(TokenKind::KwBoolean) ||
               check(TokenKind::KwString)  || check(TokenKind::KwChar) ||
               check(TokenKind::KwDeci);
    }

    //like match, but the token must be there, if not, it's a syntax error.
    const Token& expect(TokenKind kind, const std::string& what){ //'what' describes what we want, for the error message
        if (!check(kind)){
            error("expected" + what);
        }
        return advance();
    }

    //Reports a syntax error with the line and column position of the current token
    [[noreturn]] void error(const std::string& message) const{
        const Token& t = current();
        throw std::runtime_error(std::to_string(t.line) + ":" + std::to_string(t.column) + ": " + message);
    }

    //-----------------One function per kind of thing in the language--------------//

    //a top level item is either an Import or a Function
    NodePtr parseTopLevel(){
        if (match(TokenKind::KwImport)) return parseImport();
        if (match(TokenKind::KwFunc)) return parseFunction();
        error("expected 'import' or 'func'");
    }

    NodePtr parseImport(){
        expect(TokenKind::Less, " '<' after import");
        const Token& name = expect(TokenKind::Identifier, " a module name");
        expect(TokenKind::Greater, " '>' after the module name");
        return std::make_unique<ImportDecl>(name.text);
    }

    NodePtr parseFunction() {
        std::string name = expect(TokenKind::Identifier, "a function name").text;
        auto fn = std::make_unique<FunctionDecl>(name);
        expect(TokenKind::LParen, "'(' after the function name");


        if (!check(TokenKind::RParen)) {
            do {
                if (!isTypeKeyword()) {
                    error("expected a parameter type (Int, Deci, Char, Boolean or String)");
                }
                std::string typeName = advance().text;
                std::string paramName = expect(TokenKind::Identifier, "a parameter name").text;
                fn->params.push_back(Param{typeName, paramName});
            } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RParen, "')' to end the parameter list");

        if (match(TokenKind::Arrow)) {
            if (!isTypeKeyword()) {
                error("expected a return type after '->'");
            }
            fn->returnType = advance().text;
        }

        expect(TokenKind::LBrace, "'{' to start the function body");
        while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
            fn->body.push_back(parseStatement());
        }
        expect(TokenKind::RBrace, "'}' to end the function body");
        return fn;
    }

    //a statement will either be Type[] or Return
    NodePtr parseStatement(){
        if (match(TokenKind::KwType)) return parseTypeStmt();
        if (match(TokenKind::KwReturn)) return parseReturn();
        if (match(TokenKind::KwRead)) return parseReadStmt();
        if (match(TokenKind::KwWhile)) return parseWhileStmt();
        if (match(TokenKind::KwFor)) return parseForStmt();
        if (match(TokenKind::KwSwitch)) return parseSwitch();
        if (check(TokenKind::KwBreak)) return parseBreak();
        if (check(TokenKind::KwContinue)) return parseContinue();
        if (match(TokenKind::KwIf)) return parseIfStmt();
        if (isTypeKeyword()) return parseVarDecl();
        if (check(TokenKind::Identifier)) {
            if (callFollows()) {
                NodePtr call = parseCall();
                match(TokenKind::Semicolon);
                return call;
            }
            return parseAssignment();
        }
        error("expected a statement (Type, Read, return, a declaration, an assignment, if, while, for, break or continue)");
    }

    NodePtr parseTypeStmt(){
        expect(TokenKind::LBracket, " '[' after 'Type'");
        auto stmt = std::make_unique<TypeStmt>();

        while (!check(TokenKind::RBracket) && !check(TokenKind::EndOfFile)){
            stmt->items.push_back(parsePrimary());
        }
        expect(TokenKind::RBracket, " ']' to end the 'Type' Statement");

        if (match(TokenKind::Comma)){
            expect(TokenKind::NewlineMark, " '\\n' after the comma");
            stmt->newline = true;
        }
        match(TokenKind::Semicolon);
        return stmt;
    }

    // return value      or      return   (nothing after it)
    NodePtr parseReturn() {
        int returnLine = tokens[pos - 1].line;      // the line the 'return' keyword was on

        bool bare = check(TokenKind::RBrace) || check(TokenKind::Semicolon) ||
                    check(TokenKind::EndOfFile) || current().line != returnLine;

        NodePtr value;
        if (!bare) {
            value = parseTernary();
        }
        match(TokenKind::Semicolon);
        return std::make_unique<ReturnStmt>(std::move(value));
    }

    NodePtr parseVarDecl() {
        std::string typeName = advance().text;

        std::vector<std::string> names;
        do{
            names.push_back(expect(TokenKind::Identifier, "a variable name").text);
        }
        while (match(TokenKind::Comma));

        NodePtr init;
        if (match(TokenKind::Assign) || match(TokenKind::LeftArrow)) init = parseTernary();

        if (check(TokenKind::Comma)) error("only one '= value' is allowed, at the end and it applies to all the names");
        match(TokenKind::Semicolon);
        return std::make_unique<VarDecl>(typeName, std::move(names), std::move(init));
    }

    NodePtr parseAssignment(){
        std::string name = advance().text;

        Token arrow = current();
        if (!match(TokenKind::Assign) && !match(TokenKind::LeftArrow)){
            error("expected '=' or '<-' after the variable name");
        }

        NodePtr value = parseTernary();
        match(TokenKind::Semicolon);
        return std::make_unique<AssignStmt>(name, arrow.text, std::move(value));
    }

    NodePtr parseReadStmt(){
        expect(TokenKind::LBracket, " '[' after 'Read'");
        const Token& name = (expect(TokenKind::Identifier, "a variable name to read into"));
        expect(TokenKind::RBracket, " ']' for ending 'Read' Statement");
        match(TokenKind::Semicolon);
        return std::make_unique<ReadStmt>(name.text);
    }

    NodePtr parseBlock(){
        expect(TokenKind::LBrace, "'{' to start a block");
        auto block = std::make_unique<Block>();
        // Keep reading statements until the closing brace (or the end of the file,
        // which would be an error caught by the expect below).
        while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
            block->statements.push_back(parseStatement());
        }
        expect(TokenKind::RBrace, "'}' to end the block");
        return block;
    }

    NodePtr parseBody(){
        if (check(TokenKind::LBrace)) return parseBlock();
        auto block = std::make_unique<Block>();
        block->statements.push_back(parseStatement());
        return block;
    }

    NodePtr parseIfStmt(){
        expect(TokenKind::LParen, " '(' after 'if' condition");
        NodePtr condition = parseTernary();
        expect(TokenKind::RParen, " ')' after the condition");

        NodePtr thenBranch = parseBody();

        NodePtr elseBranch;
        if (match(TokenKind::KwElse)) {
            if (match(TokenKind::KwIf)) {
                // "else if": the else part is itself a whole if statement.
                // This recursion is what makes chains of any length work.
                elseBranch = parseIfStmt();
            } else {
                elseBranch = parseBody();      // plain "else": just a block
            }
        }
        return std::make_unique<IfStmt>(std::move(condition), std::move(thenBranch), std::move(elseBranch));
    }

    NodePtr parseWhileStmt(){
        expect(TokenKind::LParen, " '(' after 'while'");
        NodePtr condition = parseTernary();
        expect(TokenKind::RParen, " ')' after the condition");
        loopDepth++;
        NodePtr body = parseBody();
        loopDepth--;
        return std::make_unique<WhileLoop>(std::move(condition), std::move(body));
    }

    NodePtr parseForStmt()
    {
        expect(TokenKind::LParen, " '(' after 'for'");

        NodePtr init;
        if (check(TokenKind::Semicolon)){
            advance();
        } else{
            if (isTypeKeyword())                    init = parseVarDecl();
            else if (check(TokenKind::Identifier))  init = parseAssignment();
            else error("expected a declaration or assignment of 'for'");

            if (!lastWasSemicolon()) error("expected ';' after the first part of the for header");
        }
        NodePtr condition;
        if (!check(TokenKind::Semicolon)){
            condition = parseTernary();
        }
        expect(TokenKind::Semicolon, " ';' after the condition of for the header");

        NodePtr update;
        if (!check(TokenKind::RParen)){
            if (check(TokenKind::Identifier)) update = parseAssignment();
            else error("expected an assignment for the last part of 'for'");
        }
        expect(TokenKind::RParen, " ')' to end the 'for' header");

        loopDepth++;
        NodePtr body = parseBody();
        loopDepth--;
        return std::make_unique<ForLoop>(std::move(init), std::move(condition), std::move(update), std::move(body));
    }

    NodePtr parseBreak(){
        if (loopDepth == 0) error("' break' can only be used inside a loop");
        advance();
        match(TokenKind::Semicolon);
        return std::make_unique<BreakStmt>();
    }

    NodePtr parseContinue(){
        advance();
        match(TokenKind::Semicolon);
        return std::make_unique<ContinueStmt>();
    }

    NodePtr parseSwitch(){
        expect(TokenKind::LParen, " '(' after 'switch'");
        NodePtr subject = parseTernary();
        expect(TokenKind::RParen, " ')' after the switch value");
        expect(TokenKind::LBrace, " '{' to start the switch body");

        auto sw = std::make_unique<SwitchStmt>(std::move(subject));
        bool sawDefault = false;

    while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)){
        auto clause = std::make_unique<CaseClause>();

        if (match(TokenKind::KwCase)){
            do {
              clause->labels.push_back(parseExpression());
            } while (match(TokenKind::Comma));
        }
        else if (match(TokenKind::KwDefault)){
            if (sawDefault) error("a switch can only have 1 default");
            sawDefault = true;
            clause->isDefault = true;
        }   else{
            error("expected 'case' or 'default' inside of switch");
        }
        expect(TokenKind::Colon, " ':' after the case label");

        auto body = std::make_unique<Block>();
        while (!check(TokenKind::KwCase) && !check(TokenKind::KwDefault) && !check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)){
                body->statements.push_back(parseStatement());
            }
        clause->body = std::move(body);
        sw->cases.push_back(std::move(clause));
        }
        expect(TokenKind::RBrace, " '}' to end the 'switch' statement");
        return sw;
    }

    static int precedence(TokenKind kind) {
        switch (kind){
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Percent:      return 6;     // multiply, divide, remainder
        case TokenKind::Plus:
        case TokenKind::Minus:        return 5;     // add, subtract
        case TokenKind::Less:
        case TokenKind::Greater:
        case TokenKind::LessEqual:
        case TokenKind::GreaterEqual: return 4;     // ordering comparisons
        case TokenKind::EqualEqual:
        case TokenKind::BangEqual:    return 3;     // equality comparisons
        case TokenKind::AmpAmp:       return 2;     // logical AND
        case TokenKind::PipePipe:     return 1;     // logical OR
        default:                      return 0;
        }
    }

    NodePtr parseTernary() {
        NodePtr condition = parseExpression();

        if (!match(TokenKind::Question)) return condition;

        NodePtr thenValue  = parseTernary();
        expect(TokenKind::Colon, " ':' in the ? : expression");
        NodePtr elseValue  = parseTernary();

        return std::make_unique<TernaryExpr>(std::move(condition), std::move(thenValue), std::move(elseValue));
    }

    NodePtr parseExpression(int minPrec = 1) {
        NodePtr left = parsePrimary();      // always starts with one value

        while (true) {
            int prec = precedence(current().kind);

            // Stop if: the next token isn't an operator, it binds too weakly for this
            // level, or it's on a NEW line (a new line ends the expression).
            if (prec == 0 || prec < minPrec) {
                break;
            }

            Token op = advance();       // take the operator (copied, since advance moves the finger)

            // The right side may only absorb operators that bind TIGHTER than this one
            // (prec + 1). That is what makes "10 - 4 - 3" group as (10 - 4) - 3.
            NodePtr right = parseExpression(prec + 1);

            // The new node becomes the "left" for the next turn of the loop.
            left = std::make_unique<BinaryExpr>(op.text, std::move(left), std::move(right));
        }
        return left;
    }

    NodePtr parseCall() {
        std::string name = advance().text;
        expect(TokenKind::LParen, "'(' after the function name");
        auto call = std::make_unique<CallExpr>(name);

        // Arguments: zero or more values separated by commas.
        if (!check(TokenKind::RParen)) {
            do {
                call->args.push_back(parseTernary());
            } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RParen, "')' to end the call");
        return call;
    }

    NodePtr parsePrimary(){
        if (match(TokenKind::LParen)) {
            NodePtr inner = parseTernary();
            expect(TokenKind::RParen, " ')' to close the parentheses");
            return inner;
        }
        if (check(TokenKind::CharLiteral)){
            return std::make_unique<CharLit>(advance().text[0]);
        }
        if (check(TokenKind::StringLiteral)){
            return std::make_unique<StringLit>(advance().text);
        }
        if (check(TokenKind::Number)){
            return std::make_unique<NumberLit>(advance().text);
        }
        if (callFollows()) {
            return parseCall();
        }
        if (check(TokenKind::Identifier)){
            return std::make_unique<NameExpr>(advance().text);
        }
        if (match(TokenKind::KwTrue))  return std::make_unique<BoolLit>(true);
        if (match(TokenKind::KwFalse)) return std::make_unique<BoolLit>(false);
        error("expected a value, e.g. a string, number or identifier");
    }
};