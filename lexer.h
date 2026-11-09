#pragma once

#include <string>
#include "token.h"

// The lexer reads source text and hands back one token each time next_token() is called
// It keeps a "finger" (pos) on the current character, and also tracks the line and
// column of that character so every token can say where it came from
class Lexer
{
public:
    explicit Lexer(const std::string& source) : src(source) {};
    Token nextToken(){
        skip_whitespace();

        int start_line = line;
        int start_col = col;

        if (at_end()){
            return Token{TokenKind::EndOfFile, "", start_line, start_col};
        }

        char c = advance();

        switch (c)
        {
        case '(': return Token{TokenKind::LParen,    "(", start_line, start_col};
        case ')': return Token{TokenKind::RParen,    ")", start_line, start_col};
        case '{': return Token{TokenKind::LBrace,    "{", start_line, start_col};
        case '}': return Token{TokenKind::RBrace,    "}", start_line, start_col};
        case '[': return Token{TokenKind::LBracket,  "[", start_line, start_col};
        case ']': return Token{TokenKind::RBracket,  "]", start_line, start_col};
        case '<': return Token{TokenKind::Less,      "<", start_line, start_col};
        case '>': return Token{TokenKind::Greater,   ">", start_line, start_col};
        case ',': return Token{TokenKind::Comma,     ",", start_line, start_col};
        case ';': return Token{TokenKind::Semicolon, ";", start_line, start_col};
        case '#': return Token{TokenKind::Hash,      "#", start_line, start_col};
        case '+': return Token{TokenKind::Plus,      "+", start_line, start_col};
        case '-': return Token{TokenKind::Minus,     "-", start_line, start_col};
        case '*': return Token{TokenKind::Star,      "*", start_line, start_col};
        case '/': return Token{TokenKind::Slash,     "/", start_line, start_col};
        default:
            // Anything we don't recognize yet (letters, digits, quotes, backslashes)
            // is reported as Invalid. Later steps teach the lexer to handle them.
            return Token{TokenKind::Invalid, std::string(1, c), start_line, start_col};
        }
    }
private:
    std::string src;
    size_t pos = 0;
    int line = 1;
    int col = 1;

    bool at_end() const{
        return pos >= src.size();
    }

    // Looks at the current character without moving, Returns '\0' (a null character)
    // at the end so callers never read past the string
    char peek() const{
        return at_end() ? '\0' : src[pos];
    }

    char advance(){
        char c = src[pos++];    //increase position
        if (c == '\n'){
            line++;     //inc line number
            col = 1;
        }
        else {col++;}
        return c;
    }

    void skip_whitespace(){
        while (!at_end()){
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            {
                advance();
            }
            else{break;}       //if something real is found, then stop skipping
        }
    }

};