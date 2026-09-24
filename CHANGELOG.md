# Changelog

All notable changes to Pneumo are documented in this file. The project follows
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2026-09-24

### Added

- Interruptible `pnm::utils::concurrent::sleep_for` and `sleep_until` helpers with
  optional `std::stop_token` support and a boolean completion result.
- Compile-time `pnm::meta::structural::range` helpers that generate aggregates
  containing consecutive integral values.
- The `_fs` fixed-string literal, `FixedStringLike` concept, and `FixedString::empty()`.
- Revolution (`rev`) angle units and an `AngularVelocity` quantity with radians
  per second (`rad_s`), revolutions per minute (`rpm`), and angle/time relations.
- A `Ratio` quantity with fraction and percent units, quantity scaling, and
  `std::chrono::duration` multiplication and division returning `Time`.
- `Time::now<Clock>()` for obtaining time since a clock's epoch, defaulting to
  `std::chrono::steady_clock`.
- Container smoke checks covering developer tools, compiler aliases, and GCC/Clang
  sample builds as the `vscode` user.

### Changed

- Quantity storage is exposed through a public `value` member for reflection.
- CI and development images share an Ubuntu 26.04 toolchain definition with GCC 16
  and Bloomberg Clang/P2996. VS Code uses the published `pneumo-devcontainer:latest`
  image directly; project CI uses `pneumo-ci:latest`.
- Container publication on `main` updates both `latest` images, while pull requests
  validate the development image and manual builds on other branches publish
  isolated commit tags.
- CMake presets disable module scanning for the header-only library, avoiding a
  dependency on `clang-scan-deps` in the published toolchain.

## [0.1.0] - 2026-07-18

Initial public release:

- Common result, assertion, memory, concurrency, queue, and bit utilities.
- C++26 reflection helpers for tuples, variants, types, enums, structures, and source excerpts.
- Reflection-aware formatting with optional JSON, YAML, and TOML support through Glaze.
- Strongly typed units, literals, dimensional operations, and `std::chrono` interoperability.
- Asynchronous logging with level-based routing, source metadata, standard/file sinks, and custom sinks.

[0.2.0]: https://github.com/daleondev/pneumo/releases/tag/v0.2.0
[0.1.0]: https://github.com/daleondev/pneumo/releases/tag/v0.1.0
