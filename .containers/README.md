# Pneumo containers

`Dockerfile` defines both environments on Ubuntu 26.04:

- `ci` installs GCC 16 and builds the pinned Bloomberg Clang/P2996 toolchain, including
  matching libc++, clang-tidy, and LLD. It smoke-tests reflection with both compilers.
- `devcontainer` extends the published CI image with GDB, Python, SSH, sudo, and zsh,
  and creates the `vscode` user. The development image inherits the CI compiler
  toolchain; installing development packages can update Ubuntu system libraries.
  The `gcc`, `g++`, `cc`, and `c++` commands select GCC 16.

The images target `linux/amd64`. The devcontainer explicitly selects this platform;
ARM hosts need Docker's AMD64 emulation. Native ARM toolchains are not provided.

## Opening the devcontainer

Open the repository in VS Code and select **Dev Containers: Rebuild and Reopen in
Container**. `.devcontainer/devcontainer.json` selects the `devcontainer` target.
BuildKit pulls the digest-pinned `pneumo-ci` image and builds only the development
layer; it does not compile LLVM. This also works before the new
`pneumo-devcontainer` image has been published.

The CI package must be public or accessible to your Docker login. For a private
package, authenticate to `ghcr.io` on the host before opening the container.
GitHub workflows authenticate separately using `GITHUB_TOKEN`.

The workspace is explicitly mounted at `/workspaces/pneumo`, regardless of the
local checkout directory's name, with editor sessions running as `vscode`.
Editor extensions and runtime options remain in `devcontainer.json`.
The shared CMake presets disable module scanning because the library is
header-only and the published toolchain does not include `clang-scan-deps`.
CI additionally enables the optional serialization features in its configure step.

## Image publishing and pull requests

The **Build Container Images** workflow:

1. On pull requests touching container configuration, builds the development
   layer using the existing published toolchain and runs `.containers/smoke-test.sh`
   as `vscode`. The check exercises sudo, zsh, GDB, Python virtual environments,
   compiler aliases, and the common/meta samples with the actual GCC and Clang
   presets. It mounts the checkout read-only and builds in `/tmp`. PRs never
   publish images or rebuild LLVM.
2. On relevant pushes to `main` or manual dispatch, builds and publishes the `ci`
   target to `ghcr.io/daleondev/pneumo-ci`.
3. Builds and tests the devcontainer against that exact CI image digest, then
   publishes it to `ghcr.io/daleondev/pneumo-devcontainer` under the matching tag.

Main builds maintain the versioned CI tag and the existing CI `latest` alias.
Manual dispatches on other branches publish only `sha-<commit>` tags, so they
cannot replace the images used by normal development and CI. Normal project CI
continues to consume its existing versioned image independently of image publishing.

The prebuilt development image is also available for direct Docker use. The
checked-in devcontainer builds the small development layer locally so edits to
that layer can be tested before publication.

## Toolchain updates

Keep image publication and adoption in this order to avoid missing-image failures:

1. Update `CLANG_P2996_COMMIT` or other toolchain settings in `Dockerfile`. Choose
   a new versioned tag in the workflow's main-branch `IMAGE_TAG` expression.
   Leave the Dockerfile's default `TOOLCHAIN_IMAGE` and the main CI consumer tag
   pointing at the previous published image for now.
2. To validate the complete compiler build before merging, manually dispatch
   **Build Container Images** on the branch. This publishes isolated SHA tags and
   tests the devcontainer against the newly built CI image.
3. Merge to `main` and wait for the new versioned images to publish successfully.
4. Update `TOOLCHAIN_IMAGE` in `Dockerfile` and `container.image` in
   `.github/workflows/cmake-multi-platform.yml` together, including the new digest,
   to adopt the new CI image.
   Rebuild the devcontainer afterward.

Use a new version tag for toolchain changes, even if the Clang commit stays the
same. Both consumers pin the same published image digest so rebuilding or moving
a registry tag cannot silently change their toolchain.
For initial registry setup, run the image workflow manually and make the CI
package public or grant the needed package access.

## Local builds

From the repository root, build the normal development layer:

```sh
docker buildx build --load --platform linux/amd64 \
  --target devcontainer -t pneumo-devcontainer:local .containers
```

To build entirely from source without requiring a published CI image, select the
local `ci` stage as the development base. This is expensive and requires adequate
memory and disk space; compiler build parallelism defaults to two jobs:

```sh
docker buildx build --load --platform linux/amd64 \
  --target devcontainer --build-arg TOOLCHAIN_IMAGE=ci \
  --build-arg BUILD_JOBS=2 -t pneumo-devcontainer:local .containers
```

To use that source build through VS Code, temporarily add
`"args": { "TOOLCHAIN_IMAGE": "ci" }` to the `build` object in
`.devcontainer/devcontainer.json`.

Run the same development smoke check as GitHub:

```sh
docker run --rm --platform linux/amd64 --user vscode \
  --volume "$PWD:/workspaces/pneumo:ro" \
  pneumo-devcontainer:local bash /workspaces/pneumo/.containers/smoke-test.sh
```
