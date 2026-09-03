# Maintainer Guide for AI Agents

## BSTE Test Package

### Core Principles
- **Shell Compatibility**: All shell scripts MUST remain compatible with **mksh** and **toybox**.
  Avoid non-standard shell extensions.
- **Nested Subshell Model**: The test execution architecture relies on nested subshells to isolate
  failures while preserving state across lifecycle hooks.
- **Scoreboard Integrity**: Updates to `state.txt` must be atomic (temp file + rename) and followed
  by an explicit `fsync` call.
- **Platform Transitions**: Binaries delivered into the environment are expected to run on the
  device target platform. Use `bste_cc_binary` to guarantee proper transitions.

### Filesystem Layout
- **Harness**: `impl/bste/harness/`
- **Bazel Definitions**: `impl/bste/*.bzl`
- **Tests**: `tests/bste_test/`
- **User Facing Document**: `docs/BSTE.md`
