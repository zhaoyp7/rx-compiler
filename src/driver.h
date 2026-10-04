#pragma once

#include <cstddef>
#include <exception>
#include <optional>
#include <string>

#include <antlr4-runtime.h>

struct Options {
    std::string stage;
    std::string source;
    bool hasOutput = false;
    std::string output;
};

std::optional<Options> parseArgs(int argc, char** argv);
std::optional<std::string> readFile(const std::string& path);

class ErrorListener : public antlr4::BaseErrorListener {
public:
    void syntaxError(antlr4::Recognizer* recognizer, antlr4::Token* offendingSymbol,
                     size_t line, size_t charPositionInLine, const std::string& msg,
                     std::exception_ptr e) override;

    bool hasErrors() const { return count_ != 0; }
    std::size_t count() const { return count_; }

private:
    std::size_t count_ = 0;
};
