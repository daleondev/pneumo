# syntax=docker/dockerfile:1.7

# Reuse the published compiler layer for normal devcontainer builds.
# Set TOOLCHAIN_IMAGE=ci to build both targets entirely from source.
ARG TOOLCHAIN_IMAGE=ghcr.io/daleondev/pneumo-ci:clang-p2996-7220baff-gcc16@sha256:587f48bb860505957051b514828a07351b6c11556bdd31c3a7a6eeed077e0b4c
ARG UBUNTU_VERSION=26.04
ARG CLANG_P2996_COMMIT=7220baffd57ea5b0f8cf59bee494dd5b7cc2b748

FROM ubuntu:${UBUNTU_VERSION} AS clang-source

ARG CLANG_P2996_COMMIT
ARG BUILD_JOBS=2

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        ca-certificates \
        cmake \
        g++-16 \
        gcc-16 \
        git \
        lld \
        ninja-build \
        python3 \
    && rm -rf /var/lib/apt/lists/*

RUN git init /tmp/clang-p2996 \
    && git -C /tmp/clang-p2996 remote add origin https://github.com/bloomberg/clang-p2996.git \
    && git -C /tmp/clang-p2996 fetch --depth 1 origin "${CLANG_P2996_COMMIT}" \
    && git -C /tmp/clang-p2996 checkout --detach FETCH_HEAD

FROM clang-source AS clang-configured

RUN cmake \
      -S /tmp/clang-p2996/llvm \
      -B /tmp/clang-build \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=gcc-16 \
      -DCMAKE_CXX_COMPILER=g++-16 \
      -DCMAKE_INSTALL_PREFIX=/opt/clang-p2996 \
      -DCLANG_DEFAULT_CXX_STDLIB=libc++ \
      -DCLANG_DEFAULT_LINKER=lld \
      -DCLANG_INCLUDE_TESTS=OFF \
      -DLIBCXX_ENABLE_SHARED=ON \
      -DLIBCXX_ENABLE_STATIC=OFF \
      -DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=OFF \
      -DLIBCXX_INCLUDE_BENCHMARKS=OFF \
      -DLIBCXX_INCLUDE_TESTS=OFF \
      -DLIBCXXABI_ENABLE_SHARED=ON \
      -DLIBCXXABI_ENABLE_STATIC=OFF \
      -DLIBCXXABI_INCLUDE_TESTS=OFF \
      -DLIBUNWIND_ENABLE_SHARED=ON \
      -DLIBUNWIND_ENABLE_STATIC=OFF \
      -DLIBUNWIND_INCLUDE_TESTS=OFF \
      -DLLVM_BUILD_BENCHMARKS=OFF \
      -DLLVM_BUILD_EXAMPLES=OFF \
      -DLLVM_BUILD_TESTS=OFF \
      -DLLVM_BUILD_UTILS=OFF \
      -DLLVM_DISTRIBUTION_COMPONENTS="clang;clang-resource-headers;clang-tidy;lld" \
      -DLLVM_ENABLE_ASSERTIONS=OFF \
      -DLLVM_ENABLE_BINDINGS=OFF \
      -DLLVM_ENABLE_CURL=OFF \
      -DLLVM_ENABLE_LIBEDIT=OFF \
      -DLLVM_ENABLE_LIBXML2=OFF \
      -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld" \
      -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" \
      -DLLVM_ENABLE_ZLIB=OFF \
      -DLLVM_ENABLE_ZSTD=OFF \
      -DLLVM_INCLUDE_BENCHMARKS=OFF \
      -DLLVM_INCLUDE_EXAMPLES=OFF \
      -DLLVM_INCLUDE_TESTS=OFF \
      -DLLVM_INSTALL_TOOLCHAIN_ONLY=ON \
      -DLLVM_PARALLEL_LINK_JOBS=1 \
      -DLLVM_TARGETS_TO_BUILD=X86 \
      -DLLVM_USE_LINKER=lld

FROM clang-configured AS clang-builder

RUN cmake --build /tmp/clang-build --target install-distribution --parallel "${BUILD_JOBS}" \
    && cmake --build /tmp/clang-build --target install-runtimes --parallel "${BUILD_JOBS}"

FROM ubuntu:${UBUNTU_VERSION} AS ci

ARG CLANG_P2996_COMMIT

LABEL org.opencontainers.image.source="https://github.com/daleondev/pneumo" \
      org.opencontainers.image.description="GCC 16 and Clang/P2996 CI toolchain for Pneumo" \
      org.opencontainers.image.licenses="MIT" \
      io.pneumo.clang-p2996.commit="${CLANG_P2996_COMMIT}"

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        ca-certificates \
        cmake \
        g++-16 \
        gcc-16 \
        git \
        ninja-build \
    && rm -rf /var/lib/apt/lists/* \
    && ln -s /usr/bin/gcc-16 /usr/local/bin/gcc \
    && ln -s /usr/bin/g++-16 /usr/local/bin/g++

COPY --from=clang-builder /opt/clang-p2996 /opt/clang-p2996

ENV PATH="/opt/clang-p2996/bin:${PATH}" \
    LD_LIBRARY_PATH="/opt/clang-p2996/lib/x86_64-unknown-linux-gnu:/opt/clang-p2996/lib"

RUN printf '#include <meta>\nconstexpr auto reflected = ^^int;\nint main() { return 0; }\n' > /tmp/reflection-smoke.cpp \
    && g++ -std=c++26 -freflection /tmp/reflection-smoke.cpp -o /tmp/gcc-smoke \
    && clang++ -std=c++26 -freflection -fexpansion-statements -stdlib=libc++ \
        /tmp/reflection-smoke.cpp -o /tmp/clang-smoke \
    && /tmp/gcc-smoke \
    && /tmp/clang-smoke \
    && clang-tidy --version \
    && rm -f /tmp/reflection-smoke.cpp /tmp/gcc-smoke /tmp/clang-smoke

WORKDIR /workspace

# BuildKit skips the compiler stages when TOOLCHAIN_IMAGE is a registry image.
FROM ${TOOLCHAIN_IMAGE} AS devcontainer

USER root

LABEL org.opencontainers.image.description="Pneumo development environment with the CI toolchain"

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        build-essential \
        curl \
        gdb \
        gpg \
        less \
        locales \
        openssh-client \
        procps \
        python-is-python3 \
        python3 \
        python3-dev \
        python3-pip \
        python3-venv \
        sudo \
        unzip \
        vim-tiny \
        wget \
        zsh \
    && rm -rf /var/lib/apt/lists/* \
    && locale-gen en_US.UTF-8

# Ubuntu images may already reserve UID/GID 1000 for the ubuntu user.
RUN if id -u ubuntu >/dev/null 2>&1; then \
        usermod --login vscode --home /home/vscode --move-home --shell /bin/zsh ubuntu \
        && groupmod --new-name vscode ubuntu; \
    else \
        groupadd --gid 1000 vscode \
        && useradd --uid 1000 --gid vscode --create-home --shell /bin/zsh vscode; \
    fi \
    && printf 'vscode ALL=(ALL) NOPASSWD:ALL\n' > /etc/sudoers.d/vscode \
    && chmod 0440 /etc/sudoers.d/vscode \
    && visudo --check \
    && printf 'autoload -Uz compinit\ncompinit\nbindkey -e\n' > /home/vscode/.zshrc \
    && chown vscode:vscode /home/vscode/.zshrc \
    && mkdir -p /workspaces/pneumo \
    && chown vscode:vscode /workspaces/pneumo

ENV LANG=en_US.UTF-8 \
    PIP_DISABLE_PIP_VERSION_CHECK=1

WORKDIR /workspaces/pneumo

USER vscode
