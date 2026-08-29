# Privileged `CurrentUser\Root` WinTrust suite — exact host boundary and residue proof

Date: 2026-08-30
Host: Windows 11 Pro 10.0.26200 (development host; non-elevated user session)
Tree: `aad8909` (`tests(trust): install fixture roots silently and fail closed on ProtectedRoots`)
Runner: `verify/run_privileged_trust_tests.ps1` (repository-supported opt-in pathway)

## Result

`BLOCKED_ENVIRONMENT` — not a product failure, and not a policy denial of the
whole `CurrentUser\Root` store.

## What was actually measured

The boundary is narrower than previously recorded, and the discriminating
evidence is concrete.

`CurrentUser\Root\ProtectedRoots` on this host **did** contain a FastFiles test
root (`DB9213993F65FB10D173E22E868437CF2A4AE6D5`, `CN=FastFiles Test Code
Signing`), and that root chain-built successfully (`X509Chain.Build` = `True`,
no chain status entries). So the host does honour CurrentUser roots, and its
`ProtectedRoots` list can hold one.

What is blocked is *adding a new entry non-interactively*:

* `HKCU\...\SystemCertificates\Root\ProtectedRoots` is ACL-protected against
  user writes. A direct `Set-ItemProperty` returns
  `Requested registry access is not allowed.`
* The supported way to add an entry is Crypt32's "install this CA?"
  confirmation, raised by `certutil -user -f -addstore Root` and by
  `X509Store.Add`. With stdout redirected and no interactive desktop, that
  dialog never resolves and the fixture previously hung for its full 30s
  `certutil` timeout, then failed with an opaque WinTrust `0x800B0109`
  (`CERT_E_UNTRUSTEDROOT`).

Writing the thumbprint into the `ProtectedRoots` blob directly would bypass
that confirmation. That is the storage behind a deliberate Windows trust gate,
so it was **not** implemented — doing so would weaken the trust boundary the
campaign requires preserving.

## Current behaviour

`Add-CertificateToCurrentUserRoot` now writes the encoded certificate silently
via `CERT_STORE_PROV_REG` on the HKCU Root key (no dialog), then verifies
`ProtectedRoots` membership and fails closed:

```
AUTHENTICODE_FIXTURE_PROTECTEDROOTS_UI_REQUIRED
PACKAGE_TRUST_FIXTURE_PROTECTEDROOTS_UI_REQUIRED
```

Failure is now ~5s with a named reason instead of a ~30s hang followed by a
misleading untrusted-root verdict.

## Suite outcome

```
0% tests passed, 3 tests failed out of 3
  24 - ffengine_authenticode_wintrust_integration_tests
  25 - ffengine_authenticode_wintrust_integration_timestamp_tests
  85 - ffsetup_package_trust_wintrust_integration_tests
```

Every failing assertion is a fixture-setup assertion ("fixture prepares a
trusted CurrentUser root", "short-lived test certificate is generated and
signed"). No assertion about a **product** WinTrust decision failed. All
cleanup and residue assertions passed.

## Residue proof

`CurrentUser\{My,Root,CA,TrustedPublisher}` were snapshotted before and after
the run and compared thumbprint-by-thumbprint:

```
IDENTICAL to clean baseline - zero residue across My/Root/CA/TrustedPublisher
```

A direct `Prepare` probe confirms the same at fixture level: exit 1 with the
named reason above, zero leftover files in the isolated temp directory, zero
leftover `CN=FastFiles*` certificates in any store.

The runner's own residue check covers `CurrentUser\Root` and `CurrentUser\My`
but **not** `CurrentUser\CA`; the independent snapshot comparison above closes
that gap for this run.

## Pre-existing residue removed

Earlier runs — whose cleanup was blocked before the fail-closed path existed —
had leaked six certificates onto this host. These were removed with the user's
explicit approval, matched by exact thumbprint, before the clean baseline was
taken:

| Store | Thumbprint | Subject |
| --- | --- | --- |
| Root + My (private key) + CA | `DB9213993F65FB10D173E22E868437CF2A4AE6D5` | `CN=FastFiles Test Code Signing` |
| CA | `B150B22F694E757D55387311A2DF2D1FDAD0F9B0` | `CN=FastFiles TEST CERTIFICATE NON-PRODUCTION runtime silent ...` |
| CA | `9E00C63FF81BE6C9B3AAE49A2558B4C54E64C524` | `CN=FastFiles TEST CERTIFICATE NON-PRODUCTION runtime probe ...` |
| CA | `51C421D9EF717E6627A32E1775EDB1A7A85F6CB2` | `CN=FastFiles TEST CERTIFICATE NON-PRODUCTION probe ...` |
| CA | `0B804AEA445C784E3520DABE15F2EF1E6406F962` | `CN=FastFiles Runtime Validation 20260801-165338` |

`DB9213...` was the material one: a **Code Signing** EKU root, trusted and
valid to 2028-08-03, with its private key still present in `CurrentUser\My`.

Removal was verified to be identity-specific — 7 store entries removed, all
`CN=FastFiles*`, 0 non-FastFiles removals, 0 additions.

Its `ProtectedRoots` entry could not be removed (same ACL that blocks adding
one). That entry now names a thumbprint with no certificate behind it in any
store, so there is nothing left for Crypt32 to anchor a chain to.

## Known fixture limitation

`Remove-CurrentUserRootCertificate` deletes the `Root\Certificates\<thumbprint>`
subkey but does not clear the corresponding `ProtectedRoots` entry, because that
value is not user-writable. On a host where the install confirmation *can* be
driven, a completed run will leave a dangling `ProtectedRoots` thumbprint.
That is inert for the reason above, but it is not byte-for-byte restoration of
the pre-run registry state and should be stated rather than claimed otherwise.

## What would unblock this

An interactive desktop session able to answer the Crypt32 install confirmation,
or a host/image whose policy permits a non-interactive `ProtectedRoots` update
through a supported API. Neither is a code change to FastFiles.
