#include <optional>
#include <string>

#include <antlr4-runtime.h>

#include "driver.h"
#include "RxLexer.h"
#include "RxParser.h"

#include "ast/ast.h"

int main(int argc, char** argv) {
    std::optional<Options> options = parseArgs(argc, argv);
    if (!options) {
        return 1;
    }

    std::optional<std::string> source = readFile(options->source);
    if (!source) {
        return 1;
    }

    ErrorListener listener;
    antlr4::ANTLRInputStream input(*source);
    RxLexer lexer(&input);
    lexer.removeErrorListeners();
    lexer.addErrorListener(&listener);
    antlr4::CommonTokenStream tokens(&lexer);
    RxParser parser(&tokens);
    parser.removeErrorListeners();
    parser.addErrorListener(&listener);

    parser.crate();
    if (listener.hasErrors()) {
        return 1;
    }

    if (options->stage == "parse") {
        return 0;
    }
    if (options->stage == "ast") {
        return 1;
    }
    if (options->stage == "semantic") {
        return 1;
    }
    return 1;
}
