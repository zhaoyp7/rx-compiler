# P0：只接入 SEMANTIC；IR/CODEGEN 等第 8/12 周再填。
# 参考实现配置已备份到 config.reference.mk。

# 构建编译器，并留下空的 runtime（BUILD 每次 make test 只跑一次）
BUILD = cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug >/dev/null && \
    cmake --build build -j32 >/dev/null && : > {runtime}

# 语义：accept 退 0，reject 恰好退 1
SEMANTIC = ./build/rxc --stage semantic {source}

# 以下三条留给后续阶段
IR =
CODEGEN =
RUN = xmake run -P vendor/REIMU reimu --memory=256M --stack=1M \
    -f {output},{runtime} -o {stdout} -p {profile} 1>&2
