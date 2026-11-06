# 已接入 SEMANTIC 与 IR；CODEGEN 等第 12 周再填。
# 参考实现配置已备份到 config.reference.mk。

# 构建编译器并复制预生成的 runtime 汇编；OJ 的 BUILD 环境不要求安装 clang-22。
BUILD = cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug >/dev/null && \
    cmake --build build -j2 >/dev/null && \
    cp runtime/runtime.s {runtime}

# 语义：accept 退 0，reject 恰好退 1
SEMANTIC = ./build/rxc --stage semantic {source}

# IR：把 LLVM IR 文本写到 {output}
IR = ./build/rxc --stage ir {source} -o {output}
CODEGEN =
RUN = xmake run -P vendor/REIMU reimu --memory=256M --stack=1M \
    -f {output},{runtime} -o {stdout} -p {profile} 1>&2
