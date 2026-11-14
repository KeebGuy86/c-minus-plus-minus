#pragma once

#include <string>
#include <unordered_map>
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
        case ',': return Token{TokenKind::Comma,     ",", start_line, start_col};
        case ';': return Token{TokenKind::Semicolon, ";", start_line, start_col};
        case '#': return Token{TokenKind::Hash,      "#", start_line, start_col};
        case '+': return Token{TokenKind::Plus,      "+", start_line, start_col};
        case '*': return Token{TokenKind::Star,      "*", start_line, start_col};
        case '/': return Token{TokenKind::Slash,     "/", start_line, start_col};
        case '?': return Token{TokenKind::Question, "?", start_line, start_col};
        case ':': return Token{TokenKind::Colon,    ":", start_line, start_col};
        case '\\':
            // A backslash outside a string. In C++, '\\' means one backslash character.
            // Only \n means something here: the "add a newline" marker after Type[...].
            if (peek() == 'n') {
                advance();      // take the 'n' that follows, so both characters become one token
                return Token{TokenKind::NewlineMark, "\\n", start_line, start_col};
            }
            // A backslash followed by anything else isn't part of the language.
            return Token{TokenKind::Invalid, "\\", start_line, start_col};
        case '"':
            //a double qoute means that a string is starting
            return read_string(start_line, start_col);
        case '<':
            // "<=" is one token; a lone "<" stays Less (also used by import <io>)
            if (match('=')) return Token{TokenKind::LessEqual, "<=", start_line, start_col};
            return Token{TokenKind::Less, "<", start_line, start_col};

        case '>':
            if (match('=')) return Token{TokenKind::GreaterEqual, ">=", start_line, start_col};
            return Token{TokenKind::Greater, ">", start_line, start_col};

        case '-':
            // "->" is the arrow in function headers; a lone "-" is subtraction
            if (match('>')) return Token{TokenKind::Arrow, "->", start_line, start_col};
            return Token{TokenKind::Minus, "-", start_line, start_col};

        case '=':
            // "==" compares two values; a lone "=" assigns
            if (match('=')) return Token{TokenKind::EqualEqual, "==", start_line, start_col};
            return Token{TokenKind::Assign, "=", start_line, start_col};

        case '!':
            if (match('=')) return Token{TokenKind::BangEqual, "!=", start_line, start_col};
            return Token{TokenKind::Bang, "!", start_line, start_col};

        case '&':
            // Only "&&" exists in the language; a lone "&" isn't valid yet
            if (match('&')) return Token{TokenKind::AmpAmp, "&&", start_line, start_col};
            return Token{TokenKind::Invalid, "&", start_line, start_col};

        case '|':
            if (match('|')) return Token{TokenKind::PipePipe, "||", start_line, start_col};
            return Token{TokenKind::Invalid, "|", start_line, start_col};

        case '%': return Token{TokenKind::Percent, "%", start_line, start_col};
        default:
            // A letter means a word is starting, so read the whole word.
            if (is_letter(c)) {
                return read_word(c, start_line, start_col);
            }

            // A digit means a number is starting, so read the whole number.
            if (is_digit(c)) {
                return read_number(c, start_line, start_col);
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

        static const std::unordered_map<std::string, TokenKind> keywords = {
            {"import", TokenKind::KwImport},
            {"func", TokenKind::KwFunc},
            {"return", TokenKind::KwReturn},
            {"Type", TokenKind::KwType},    //output
            {"Read", TokenKind::KwRead},    //input
            // control flow
            {"if",     TokenKind::KwIf},
            {"else",   TokenKind::KwElse},
            {"while",  TokenKind::KwWhile},
            {"for",    TokenKind::KwFor},
            // types
            {"Int", TokenKind::KwInt},
            {"Deci", TokenKind::KwDeci},
            {"Boolean", TokenKind::KwBoolean},
            {"String",  TokenKind::KwString},
            {"Char",    TokenKind::KwChar},
            // Boolean values
            {"true",   TokenKind::KwTrue},
            {"false",  TokenKind::KwFalse},
        };

        auto it = keywords.find(text);
        if (it != keywords.end()){
            return Token{it->second, text, start_line, start_col};
        }

        return Token{TokenKind::Identifier, text, start_line, start_col};
    }

    // // Reads a number(only whole numbers), 'first' is the digit we already took with advance()
    Token read_number(char first, int start_line, int start_col){
        std::string text(1, first);

        while (is_digit(peek())) {
            text += advance();
        }

        return Token{TokenKind::Number, text, start_line, start_col};
    }

    //Reads a String of text, opening qoute has already been taken by advance()
    Token read_string(int start_line, int start_col){
        std::string value;

        //keep taking in characters until the closing qoute
        while (!at_end() && peek() != '\n' && peek() != '"'){
            char c = advance();

            if (c == '\\') {
                // A backslash starts an escape: the NEXT character says which one.
                // If the line or file ends right after the backslash, stop here;
                if (at_end() || peek() == '\n') {
                    break;
                }
                char e = advance();     // the character after the backslash
                switch (e) {
                case 'n':  value += '\n'; break;    // \n becomes a real newline
                case 't':  value += '\t'; break;    // \t becomes a real tab
                case '\\': value += '\\'; break;    // \\ becomes one backslash
                case '"':  value += '"';  break;    // \" becomes a quote inside the string
                default:                            // anything else: keep it as written
                    value += '\\';
                    value += e;
                    break;
                }
            }
            else {
                value += c;     // an ordinary character goes straight in
            }
        }

        //if the string was never closed, report it as Invalid.
        if (at_end() || peek() != '"'){
            return Token{TokenKind::Invalid, value, start_line, start_col};
        }

        advance();
        return Token{TokenKind::StringLiteral, value, start_line, start_col};
    }

    bool at_end() const{
        return pos >= src.size();
    }

    // Looks at the current character without moving, Returns '\0' (a null character)
    // at the end so callers never read past the string
    char peek() const{
        return at_end() ? '\0' : src[pos];
    }

    bool match(char expected){
        if (at_end() || peek() != expected){
            return false;
        }
        advance();
        return true;
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