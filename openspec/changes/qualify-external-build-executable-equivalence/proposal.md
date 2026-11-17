# Qualify external-build executable equivalence

## Why

The actual isolated CMake build's untouched compile database makes `Test-ExecutableTreeEquivalence.ps1` reject five generated version RCs as missing Git inputs. Its generated-file classification assumes `repo/build`, and it resolves relative compile paths against the wrong directory. The current audit cannot carry source certification across documentation-only commits using this gate. A new deterministic owned fixture reproduces both positive-case failures.

## What Changes

- Recognize generated artifacts only within a CMake binary root whose cache identifies this source tree and its actual cache location.
- Resolve compile paths from the recorded compilation working directory, retain tracked-source precedence and fail-closed missing-input/header/resource/template checks.
- Register portable adversarial regression fixtures and qualify the real isolated build without filtering or editing its compile database.

## Impact

Verification harness and tests only; no application/IPC/signing/service changes. Source-tree equivalence is not binary reproducibility or production-release authorization. Existing historical certification records remain unchanged.
