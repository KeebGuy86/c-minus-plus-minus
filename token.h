#pragma once            // include this file only once, even if several files #include it

#include <string>

// Every kind of token the lexer can produce.
// 'enum class' keeps the names inside TokenKind:: so they can't clash with other code.
enum class TokenKind {
    // keywords: words with a fixed meaning in the language
    KwImport,           // import   (your INCLUDE_OR_IMPORT)
    KwFunc,             // func
    KwReturn,           // return
    KwType,             // Type     (the output statement)
    KwIf,               // if
    KwElse,             // else
    KwFor,              // for
    KwWhile,            // while

    // type names and literals: my guess at what these mean, tell me if I'm wrong
    KwInt,          // your Integer
    KwDeci,            //your Decimal
    KwBoolean,          // your Boolean
    KwChar,        // your Char
    KwString,           // String  (the type; the String below is a string literal)
    KwRead,             // Read    (input statement)
    KwTrue,             // true
    KwFalse,            // false

    // things whose text varies from program to program
    Identifier,         // names like main or name (your Name)
    Number,             // 0, 42
    StringLiteral,             // "Hello World!"  (your StringLiteral)

    // brackets
    LParen, RParen,     // ( )   your open/close_circle_bracket
    LBrace, RBrace,     // { }   your open/close_curly_bracket
    LBracket, RBracket, // [ ]   your open/close_square_bracket

    // punctuation
    Less, Greater,      // < >   (import <io> now, comparisons later)
    Comma,              // ,
    Semicolon,          // ;
    Hash,               // #     (your HASHTAG)
    NewlineMark,        // a \n written outside a string (your endln)

    // operators
    Plus, Minus, Star, Slash,

    Percent,            // %
    Assign,             // =
    EqualEqual,         // ==
    Bang,               // !
    BangEqual,          // !=
    LessEqual,          // <=
    GreaterEqual,       // >=
    AmpAmp,             // &&
    PipePipe,           // ||
    Arrow,              // ->

    //conditional tokens
    Question,           // ?   (ternary: condition ? a : b)
    Colon,              // :   (ternary: the divider between the two results)

    EndOfFile,          // marks that the input has run out
    Invalid             // a character the language doesn't recognize
};

// One token: what kind it is, the exact text it came from, and where it was found.
// The position matters later: error messages need to say "line 3, column 7".
struct Token {
    TokenKind kind;
    std::string text;   // e.g. "Hello," for a String, "name" for an Identifier
    int line;           // 1-based line number in the source file
    int column;         // 1-based column number
};

inline const char* kind_name(TokenKind k)
{
    switch (k){
        case TokenKind::KwImport: return "KwImport";
        case TokenKind::KwFunc: return "KwFunc";
        case TokenKind::KwReturn: return "KwReturn";
        case TokenKind::KwType:      return "KwType";
        case TokenKind::KwIf:        return "KwIf";
        case TokenKind::KwElse:      return "KwElse";
        case TokenKind::KwFor:       return "KwFor";
        case TokenKind::KwWhile:     return "KwWhile";
        case TokenKind::KwInt:   return "KwInt";
        case TokenKind::KwDeci: return "KwDeci";
        case TokenKind::KwBoolean:   return "KwBoolean";
        case TokenKind::KwChar: return "KwChar";
        case TokenKind::Identifier:  return "Identifier";
        case TokenKind::Number:      return "Number";
        case TokenKind::StringLiteral:      return "String";
        case TokenKind::KwString:    return "KwString";
        case TokenKind::KwRead:      return "KwRead";
        case TokenKind::KwTrue:      return "KwTrue";
        case TokenKind::KwFalse:     return "KwFalse";
        case TokenKind::LParen:      return "LParen";
        case TokenKind::RParen:      return "RParen";
        case TokenKind::LBrace:      return "LBrace";
        case TokenKind::RBrace:      return "RBrace";
        case TokenKind::LBracket:    return "LBracket";
        case TokenKind::RBracket:    return "RBracket";
        case TokenKind::Less:        return "Less";
        case TokenKind::Greater:     return "Greater";
        case TokenKind::Comma:       return "Comma";
        case TokenKind::Semicolon:   return "Semicolon";
        case TokenKind::Hash:        return "Hash";
        case TokenKind::NewlineMark: return "NewlineMark";
        case TokenKind::Plus:        return "Plus";
        case TokenKind::Minus:       return "Minus";
        case TokenKind::Star:        return "Star";
        case TokenKind::Slash:       return "Slash";
        case TokenKind::EndOfFile:   return "EndOfFile";
        case TokenKind::Invalid:     return "Invalid";
        case TokenKind::Percent:      return "Percent";
        case TokenKind::Assign:       return "Assign";
        case TokenKind::EqualEqual:   return "EqualEqual";
        case TokenKind::Bang:         return "Bang";
        case TokenKind::BangEqual:    return "BangEqual";
        case TokenKind::LessEqual:    return "LessEqual";
        case TokenKind::GreaterEqual: return "GreaterEqual";
        case TokenKind::AmpAmp:       return "AmpAmp";
        case TokenKind::PipePipe:     return "PipePipe";
        case TokenKind::Arrow:        return "Arrow";
        case TokenKind::Question:     return "Question";
        case TokenKind::Colon:        return "Colon";
    }
    return "Unknown Token";
}