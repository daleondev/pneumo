#!/usr/bin/env bash
set -euo pipefail

# Run with the repository mounted read-only to catch accidental dependencies on
# root access or a writable checkout. All generated files go into /tmp.
test "$(id -un)" = vscode
test "$HOME" = /home/vscode
test -w "$HOME"
sudo -n true
zsh -ic '[[ -w $HOME && -n $ZSH_VERSION ]]'
gdb --batch -ex quit
git --version
ssh -V
/opt/clang-p2996/bin/clang-tidy --version

smoke_dir=$(mktemp -d)
trap 'rm -rf "$smoke_dir"' EXIT
python3 -m venv "$smoke_dir/venv"
"$smoke_dir/venv/bin/python" -m pip --version

for preset in gcc-debug clang-debug; do
    cmake --preset "$preset" -S /workspaces/pneumo \
        -B "$smoke_dir/$preset" -DPNM_BUILD_TESTS=OFF
    cmake --build "$smoke_dir/$preset" \
        --target common_sample meta_sample --parallel 2
    "$smoke_dir/$preset/samples/common_sample"
    "$smoke_dir/$preset/samples/meta_sample"
done
