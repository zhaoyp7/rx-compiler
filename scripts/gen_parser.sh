#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JAR="$ROOT/tools/antlr-4.13.2-complete.jar"
GEN_DIR="$ROOT/src/generated"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if [ ! -f "$JAR" ]; then
    echo "error: ANTLR jar not found: $JAR" >&2
    echo "run: curl -fsSL -o tools/antlr-4.13.2-complete.jar https://www.antlr.org/download/antlr-4.13.2-complete.jar" >&2
    exit 1
fi

# C++ target: generated classes Lexer/Parser clash with antlr4::Lexer/antlr4::Parser,
# so regenerate from renamed grammar copies.
sed 's/^lexer grammar Lexer;/lexer grammar RxLexer;/' \
    "$ROOT/grammar/Lexer.g4" > "$TMP/RxLexer.g4"
sed 's/^parser grammar Parser;/parser grammar RxParser;/; s/tokenVocab=Lexer;/tokenVocab=RxLexer;/' \
    "$ROOT/grammar/Parser.g4" > "$TMP/RxParser.g4"

mkdir -p "$GEN_DIR"
rm -f "$GEN_DIR"/RxLexer.* "$GEN_DIR"/RxParser.*

java -jar "$JAR" -Dlanguage=Cpp -visitor -no-listener \
    -o "$GEN_DIR" "$TMP/RxLexer.g4" "$TMP/RxParser.g4"

echo "generated into $GEN_DIR:"
ls -1 "$GEN_DIR"
