#include <fstream>
#include <iostream>
#include <optional>
#include <string>

#include <antlr4-runtime.h>

#include "driver.h"
#include "RxLexer.h"
#include "RxParser.h"

#include "ast/builder.h"
#include "ast/dump.h"
#include "ir/generator.h"
#include "sema/analyzer.h"

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

    RxParser::CrateContext* tree = parser.crate();
    if (listener.hasErrors()) {
        return 1;
    }

    if (options->stage == "parse") {
        return 0;
    }
    if (options->stage == "ast") {
        rx::ASTBuilder builder;
        rx::BuildResult result = builder.build(tree);
        if (!result.ok) {
            return 1;
        }
        rx::DumpVisitor dumper(std::cout);
        dumper.dump(result.crate);
        return 0;
    }
    if (options->stage == "semantic") {
        rx::ASTBuilder builder;
        rx::BuildResult result = builder.build(tree);
        if (!result.ok) {
            return 1;
        }
        rx::SemanticAnalyzer analyzer;
        return analyzer.run(result.crate) ? 0 : 1;
    }
    if (options->stage == "ir") {
        if (!options->hasOutput) {
            std::cerr << "--stage ir requires -o <output>\n";
            return 1;
        }
        rx::ASTBuilder builder;
        rx::BuildResult result = builder.build(tree);
        if (!result.ok) {
            return 1;
        }
        rx::SemanticAnalyzer analyzer;
        if (!analyzer.run(result.crate)) {
            return 1;
        }
        std::ofstream output(options->output, std::ios::binary);
        if (!output) {
            std::cerr << "cannot open output file: " << options->output << "\n";
            return 1;
        }
        rx::IrGenerator generator(result.crate, analyzer);
        if (!generator.generate(output)) {
            std::cerr << "failed to generate IR\n";
            return 1;
        }
        output.flush();
        return output ? 0 : 1;
    }
    return 1;
}
