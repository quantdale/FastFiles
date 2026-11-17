## 1. Reproduction and design

- [x] 1.1 Reconcile live Git/CI and preserve the actual out-of-tree guard failure without claiming equivalence.
- [x] 1.2 Add real-child fixtures reproducing external-build and relative-path positive failures; define provenance and fail-closed controls. `equivalence/fixtures-red.log` retains two positive failures and blocked negative-content assertions; the conventional internal arm passes.

## 2. Implementation and regression

- [x] 2.1 Bind generated-file classification to verified CMake source/binary cache roots with tracked-input precedence and existence checks.
- [x] 2.2 Resolve relative paths against the recorded compilation directory and preserve recursive/content/symmetric coverage.
- [x] 2.3 Register and pass all positive/adversarial fixtures, including tracked inputs inside the binary root and missing/sibling/path-escape inputs. New portable CTest, final 21/21 assertions; see evidence/qualification.md.

## 3. Qualification and publication

- [x] 3.1 Prove the actual da4111e implementation-to-documentation comparison with untouched external Debug and optimized compile databases; retain machine-readable coverage/content verdicts. Both da4111e -> d6fe665 comparisons PASS: 425 paths/355 TUs/five generated RCs, equal digest, complete coverage. See evidence/qualification.md.
- [x] 3.2 Run active diagnostics, focused fixtures, full portable Debug/optimized CTest suites, strict OpenSpec and policy/drift/lifecycle gates; review exact diff for weakened checks. Full suites 147/147 each, final fixtures 21/21, two PowerShell files LSP-clean, policy/strict/lifecycle PASS; evidence/qualification.md.
- [ ] 3.3 Commit/push normally, refresh post-commit identity bindings and inspect exact-SHA hosted CI through terminal outcome. Close this bounded change using immutable implementation-bound evidence; broader audit and production readiness remain separate.
