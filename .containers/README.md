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

Open the repository in VS Code and select **Dev Containers: Reopen in Container**.
`.devcontainer/devcontainer.json` uses
`ghcr.io/daleondev/pneumo-devcontainer:latest` directly. VS Code starts the published
development environment without building the project's Dockerfile locally.

The development image must have been published before opening the container.
For the first `latest` publication, merge the image workflow changes to `main`
and let **Build Container Images** finish, or manually dispatch that workflow on
`main` after the changes are present. Dispatching on another branch only publishes
SHA tags and does not create or update `latest`.

The development package must be public or accessible to your Docker login. For a
private package, authenticate to `ghcr.io` on the host before opening the container.
GitHub workflows authenticate separately using `GITHUB_TOKEN` and need access to
the CI package.

To update an existing local environment, run this on the Docker host, then select
**Dev Containers: Rebuild Container** in VS Code:

```sh
docker pull --platform linux/amd64 ghcr.io/daleondev/pneumo-devcontainer:latest
```

Publishing a new `latest` image does not change an already running container.

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

Main builds maintain versioned tags and the `latest` alias for both images.
Manual dispatches on other branches publish only `sha-<commit>` tags, so they
cannot replace the images used by normal development and CI. Normal project CI
uses `pneumo-ci:latest`; VS Code uses `pneumo-devcontainer:latest`. The publishing
workflow still uses the exact newly built CI digest when building and validating
the development image so a moved tag cannot change its base midway through a run.

Project CI and image publishing run independently. A project CI job that starts
before publication completes can use the previous image. The two `latest` tags
are published separately, so a failed development-image build can leave CI ahead
of the published development environment. After successful publication, rerun
project CI to check the new toolchain and refresh your local container as above.

## Toolchain updates

1. Update `CLANG_P2996_COMMIT` or other toolchain settings in `Dockerfile`. Choose
   a new versioned tag in the workflow's main-branch `IMAGE_TAG` expression.
2. To validate the complete compiler build before merging, manually dispatch
   **Build Container Images** on the branch. This publishes isolated SHA tags and
   tests the devcontainer against the newly built CI image.
3. Merge to `main` and wait for both `latest` images to publish successfully.
4. Pull the development image and recreate your devcontainer. Subsequent project
   CI jobs use the published CI `latest` image without a digest-update commit.

With mutable `latest` tags, the same source commit can use different toolchains
over time. Use a versioned tag or digest explicitly when reproducing an older
environment. Development-only Dockerfile changes follow the same publication
process; editing the file alone does not change the running VS Code environment.

## Local builds

For testing Dockerfile edits before publication, optionally build the development
image locally from the repository root:

```sh
docker buildx build --pull --load --platform linux/amd64 \
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

To use either local build through VS Code, temporarily set `image` in
`.devcontainer/devcontainer.json` to `pneumo-devcontainer:local`, then rebuild the
container. Restore the published image reference before committing.

Run the same development smoke check as GitHub:

```sh
docker run --rm --platform linux/amd64 --user vscode \
  --volume "$PWD:/workspaces/pneumo:ro" \
  pneumo-devcontainer:local bash /workspaces/pneumo/.containers/smoke-test.sh
```
