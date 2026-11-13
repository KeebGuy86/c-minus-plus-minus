#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include "token.h"
#include "lexer.h"
#include "parser.h"
#include "ast.h"

using namespace std;

int main(int argc, char* argv[]) {

    if (argc < 2) {
        cout << "Usage: " << argv[0] << " <inputfile>" << endl;
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        string inputFile = argv[i];
        cout << "Processing file: " << inputFile << endl;

        ifstream file(inputFile);
        if (!file) {
            cout << "Error: Could not open file " << inputFile << endl;
            continue;
        }

        // Read the whole file into one string.
        stringstream buffer;
        buffer << file.rdbuf();
        string source = buffer.str();
        file.close();


        // Stage 1: the lexer turns the text into a list of tokens.
        Lexer lexer(source);
        vector<Token> tokens;
        while (true) {
            Token t = lexer.nextToken();
            tokens.push_back(t);
            if (t.kind == TokenKind::EndOfFile) {
                break;      // the list always ends with EndOfFile
            }
        }

        // Stage 2: the parser turns the tokens into a tree.
        // If the program has a syntax error, the parser throws, and we print it.
        try {
            Parser parser(tokens);
            auto program = parser.parseProgram();
            program->print(0);
        } catch (const exception& e) {
            cout << "Syntax error at " << e.what() << "\n";
        }
    }

    return 0;
}
