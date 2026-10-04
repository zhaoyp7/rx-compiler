#include "driver.h"

#include <fstream>
#include <iostream>
#include <sstream>

namespace {

const char* kUsage =
    "usage: rxc --stage <parse|ast|semantic|ir|codegen> <source> [-o <output>]";

bool isKnownStage(const std::string& stage) {
    return stage == "parse" || stage == "ast" || stage == "semantic" ||
           stage == "ir" || stage == "codegen";
}

}  // namespace

std::optional<Options> parseArgs(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--stage") {
            if (i + 1 >= argc) {
                std::cerr << kUsage << "\n";
                return std::nullopt;
            }
            options.stage = argv[++i];
        } else if (arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << kUsage << "\n";
                return std::nullopt;
            }
            options.output = argv[++i];
            options.hasOutput = true;
        } else if (arg.size() > 1 && arg[0] == '-') {
            std::cerr << "unknown option: " << arg << "\n" << kUsage << "\n";
            return std::nullopt;
        } else if (options.source.empty()) {
            options.source = arg;
        } else {
            std::cerr << "too many source files\n" << kUsage << "\n";
            return std::nullopt;
        }
    }
    if (options.stage.empty() || options.source.empty() ||
        !isKnownStage(options.stage)) {
        std::cerr << kUsage << "\n";
        return std::nullopt;
    }
    return options;
}

std::optional<std::string> readFile(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        std::cerr << "cannot open source file: " << path << "\n";
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

void ErrorListener::syntaxError(antlr4::Recognizer*, antlr4::Token*, size_t line,
                                size_t charPositionInLine, const std::string& msg,
                                std::exception_ptr) {
    ++count_;
    std::cerr << line << ":" << charPositionInLine << ": " << msg << "\n";
}
