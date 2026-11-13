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
    bool check(TokenKind kind) const {
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

    NodePtr parseFunction(){
        const Token& name = expect(TokenKind::Identifier, " a function name");
        expect(TokenKind::LParen, " '(' after a function name");
        expect(TokenKind::RParen, " ')'");
        expect(TokenKind::LBrace, " '{' to the start the function body");

        auto fn = std::make_unique<FunctionDecl>(name.text);

        //The body is statements until the closing bracket
        while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)){
                fn->body.push_back(parseStatement());
        }
        expect(TokenKind::RBrace, " '}' after the function body");
        return fn;
    }

    //a statement will either be Type[] or Return
    NodePtr parseStatement(){
        if (match(TokenKind::KwType)) return parseTypeStmt();
        if (match(TokenKind::KwReturn)) return parseReturn();
        error("expected a statement (Type or return)");
    }

    //Type["Hello World!"], \n              the ", \n" part is optional
    NodePtr parseTypeStmt(){
        expect(TokenKind::LBracket, " '[' after 'Type'");
        auto stmt = std::make_unique<TypeStmt>();

        //the items inside [] are expressions, until the closing bracket
        while (!check(TokenKind::RBracket) && !check(TokenKind::EndOfFile)){
            stmt->items.push_back(parseExpression());
        }
        expect(TokenKind::RBracket, " ']' to end the 'Type' Statement");

        //", \n" after the bracket means, print a newline after the Type[]
        if (match(TokenKind::Comma)){
            expect(TokenKind::NewlineMark, " '\\n' after the comma");
            stmt->newline = true;
        }
        match(TokenKind::Semicolon);    // a ';' is allowed here but not required
        return stmt;
    }

    //return 0;
    NodePtr parseReturn(){
        NodePtr value = parseExpression();
        match(TokenKind::Semicolon);    //the ';' is allowed here but not required
        return std::make_unique<ReturnStmt>(std::move(value));
    }

    //just one value for now, a string, a name, or a number
    NodePtr parseExpression(){
        if (check(TokenKind::StringLiteral)){
            return std::make_unique<StringLit>(advance().text);
        }
        if (check(TokenKind::Number)){
            return std::make_unique<NumberLit>(advance().text);
        }
        if (check(TokenKind::Identifier)){
            return std::make_unique<NameExpr>(advance().text);
        }
        error("expected a value, e.g. a string, number or identifier");
    }
};