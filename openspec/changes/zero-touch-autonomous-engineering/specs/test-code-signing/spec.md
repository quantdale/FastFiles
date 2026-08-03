## ADDED Requirements

### Requirement: Isolated Test Code-Signing Certificate
The harness SHALL generate (or accept a user-provided) isolated self-signed test code-signing certificate stored outside source control, and SHALL sign the product binaries (`FastFiles.exe`, `FastFilesEngine.exe`, `FastFilesIndexSvc.exe`, `FastFilesSetup.exe`) with it, without requiring an externally procured production certificate and without disabling product Authenticode verification.

#### Scenario: A dev build is signed without a production cert
- **WHEN** the test-code-signing capability runs and no production certificate is provided
- **THEN** it SHALL generate/use an isolated test cert, sign the product binaries, and the signed build SHALL verify against the test cert's thumbprint

#### Scenario: The test cert and key are never committed
- **WHEN** the signing capability generates a cert or exports a key
- **THEN** the cert, private key, and PFX SHALL be written to a gitignored out-of-source location and SHALL NOT be committed to the repository

### Requirement: Signature And Pinned-Thumbprint Verification
The harness SHALL verify that each product binary carries a valid Authenticode signature and SHALL assert the pinned-thumbprint behavior the product already enforces, so a signed dev build resolves the existing `binary-authenticode` capability to PASS rather than being masked.

#### Scenario: A signed build passes the Authenticode gate
- **WHEN** the product binaries are signed with the test cert
- **THEN** signature verification SHALL report the binaries as signed and the pinned-thumbprint assertion SHALL pass for the configured test thumbprint

### Requirement: Injectable Production Certificate Gate
The harness SHALL treat a production OV/EV certificate as an injectable secret or artifact (environment variables or a sealed artifact path) and SHALL run an automated production-signing gate only when the production cert is present; when absent, it SHALL report `SKIPPED(production-cert-not-provided)` and SHALL NOT fabricate a production signature, SHALL NOT globally disable signature verification, and SHALL keep production-trust availability separate from software correctness.

#### Scenario: Production signing runs only when a production cert is present
- **WHEN** a production certificate is injected
- **THEN** the production-signing gate SHALL sign and verify the binaries against it and SHALL record the production-trust outcome

#### Scenario: Absence of a production cert is not a fabricated pass
- **WHEN** no production certificate is provided
- **THEN** the production-signing gate SHALL report `SKIPPED(production-cert-not-provided)` and SHALL NOT report a production-trust pass, while the test-PKI software-correctness checks proceed independently