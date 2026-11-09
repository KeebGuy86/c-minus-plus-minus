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
            // A letter means a word is starting, so read the whole word.
            if (is_letter(c)) {
                return read_word(c, start_line, start_col);
            }
            // Anything else is still Invalid for now.
            return Token{TokenKind::Invalid, std::string(1, c), start_line, start_col};
        }
    }
private:
    std::string src;
    size_t pos = 0;
    int line = 1;
    int col = 1;

    static bool is_letter(char c){
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }

    static bool is_digit(char c){
        return c >= '0' && c <= '9';
    }

    //Reads the rest of the word, given that 'first' is a letter already taken in advance()
    Token read_word(char first, int start_line, int start_col){
        std::string text(1, first);

        while (is_letter(peek()) || is_digit(peek())){
            text += advance();
        }

        return Token{TokenKind::Identifier, text, start_line, start_col};
    }

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