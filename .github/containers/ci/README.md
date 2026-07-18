# Pneumo CI toolchain

This image provides the two Linux toolchains required by the project:

- GCC 16 from Ubuntu 26.04.
- Bloomberg Clang/P2996, its matching libc++, and clang-tidy, built from the pinned commit in the
  `Dockerfile`.

The expensive compiler build is published once to `ghcr.io/daleondev/pneumo-ci` by
`build-ci-image.yml`. Normal CI jobs consume the versioned image and do not compile LLVM themselves.

## Bootstrapping and updates

1. Run the **Build CI Toolchain Image** workflow once before enabling the main required CI check.
2. Set the resulting GHCR package visibility to public, or grant this repository read access to the
   package. The workflows also authenticate with `GITHUB_TOKEN` for private-package access.
3. When updating Clang/P2996, change the full commit in the `Dockerfile`, `IMAGE_TAG` in the image
   workflow, and the matching container tag in the main CI workflow. Push the change to `main` or
   dispatch the image workflow manually.
4. After the image is published successfully, rerun **Multi-Platform CI/CD**.

The image build only targets `linux/amd64`, matching GitHub's hosted Ubuntu runners. Both compilers are
smoke-tested with `<meta>` and `-freflection` while the image is built.
