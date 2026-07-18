FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04 AS base

ARG DEBIAN_FRONTEND=noninteractive
ARG CLANG_P2996_COMMIT=7220baffd57ea5b0f8cf59bee494dd5b7cc2b748

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    gpg \
    wget \
    software-properties-common \
    && wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null \
    && echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ noble main' | tee /etc/apt/sources.list.d/kitware.list >/dev/null \
    && add-apt-repository -y ppa:ubuntu-toolchain-r/test \
    && rm -rf /var/lib/apt/lists/*

FROM base AS clang-builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    python3 \
    git \
    lld \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /llvm-project
RUN git init . \
    && git remote add origin https://github.com/bloomberg/clang-p2996.git \
    && git fetch --depth 1 origin "${CLANG_P2996_COMMIT}" \
    && git checkout --detach FETCH_HEAD

RUN cmake -S llvm -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" \
    -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" \
    -DLLVM_TARGETS_TO_BUILD="X86" \
    -DLLVM_USE_LINKER=lld \
    -DCMAKE_INSTALL_PREFIX=/opt/clang-p2996

RUN ninja -C build \
    install-clang \
    install-clang-resource-headers \
    install-clang-tidy \
    install-clang-scan-deps \
    install-cxx \
    install-cxxabi \
    install-unwind

FROM base AS devcontainer

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    gcc-16 \
    g++-16 \
    gdb \
    ninja-build \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-16 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-16 100 \
    && update-alternatives --install /usr/bin/cc cc /usr/bin/gcc 100 \
    && update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++ 100 \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install -y --no-install-recommends \
    python-is-python3 \
    python3 \
    python3-dev \
    python3-pip \
    python3-venv \
    && rm -rf /var/lib/apt/lists/*

COPY --from=clang-builder /opt/clang-p2996 /opt/clang-p2996

ENV PATH="/opt/clang-p2996/bin:${PATH}"
ENV LD_LIBRARY_PATH="/opt/clang-p2996/lib/x86_64-unknown-linux-gnu:/opt/clang-p2996/lib"
ENV PIP_DISABLE_PIP_VERSION_CHECK=1

WORKDIR /workspaces/pneumo
