#include <iostream>
#include <fstream>
#include <sstream>
#include "token.h"
#include "lexer.h"
#include "ast.h"

using namespace std;

int main(int argc, char* argv[]) {

    // TEMPORARY: build the Hello World tree by hand to test ast.h
    Program prog;
    prog.items.push_back(std::make_unique<ImportDecl>("io"));

    auto fn = std::make_unique<FunctionDecl>("main");
    auto out = std::make_unique<TypeStmt>();
    out->items.push_back(std::make_unique<StringLit>("Hello World!"));
    out->newline = true;
    fn->body.push_back(std::move(out));
    fn->body.push_back(std::make_unique<ReturnStmt>(std::make_unique<NumberLit>("0")));
    prog.items.push_back(std::move(fn));

    prog.print(0);

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


        // Run the lexer over the program and print every token it produces
        Lexer lexer(source);
        while (true) {
            Token t = lexer.nextToken();
            cout << t.line << ":" << t.column << "  "
                 << kind_name(t.kind) << "  '" << t.text << "'\n";
            if (t.kind == TokenKind::EndOfFile) {
                break;      // no more tokens, stop
            }
        }
    }

    return 0;
}
