# 已接入 SEMANTIC 与 IR；CODEGEN 等第 12 周再填。
# 参考实现配置已备份到 config.reference.mk。

# 构建编译器，并把 C runtime 编成 REIMU 可链接的汇编（BUILD 每次 make test 只跑一次）
BUILD = cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug >/dev/null && \
    cmake --build build -j2 >/dev/null && \
    clang-22 --target=riscv32-unknown-elf -march=rv32im -mabi=ilp32 \
      -O2 -fno-builtin -S runtime/runtime.c -o {runtime}

# 语义：accept 退 0，reject 恰好退 1
SEMANTIC = ./build/rxc --stage semantic {source}

# IR：把 LLVM IR 文本写到 {output}
IR = ./build/rxc --stage ir {source} -o {output}
CODEGEN =
RUN = xmake run -P vendor/REIMU reimu --memory=256M --stack=1M \
    -f {output},{runtime} -o {stdout} -p {profile} 1>&2
