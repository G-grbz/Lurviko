# Local security review

Checked against the source prepared for Lurviko v1.1.0.

| Report | Finding/action |
| --- | --- |
| Menu retains scroll | Confirmed; cancel flick and reset on every GMenu opening. Native regression added. |
| Disk-backed runtime fallback | Confirmed; plaintext previews use a unique owner-only directory on verified Linux tmpfs/ramfs. Prefer private runtime storage; fall back to `/dev/shm`, never disk-backed `/tmp`. If unavailable, refuse previews while preserving encrypted vault access/export. |
| KDF header values | Confirmed; validate integer types/ranges before Argon2. Bounds: 1–10 passes, 8–256 MiB, 1–8 lanes, 1–lanes threads. Reject rather than clamp; retain the existing 3-pass/64-MiB format. Bound header reads to 64 KiB. |
| bsdtar option injection | Confirmed; prefix each basename with `./`. Actually archive/extract dash and @ names, spaces, Unicode and multiple source directories in tests. |
| Runtime prefix matching | Confirmed; check normalized paths, directory separators and canonical parent paths, and reject symlinks. Sibling prefixes cannot be removed. |
| AES-GCM size | File streaming used one IV/tag per file; 1-MiB I/O blocks are not independent authenticated records. Enforce NIST's 2^36−32-byte invocation bound before import, during encryption of a growing file and during decryption. A segmented format requires separate design/versioning and is not improvised here. |
| Dependency/model downloads | Confirmed; exact transitive versions/SHA256 hashes, binary-wheel-only pip installs and no unconditional upgrade. Installation/downloads default off with a persistent subtitle-dialog opt-in. Full HF commits govern downloads and offline lookup. Custom remote models need a full revision; local directories remain supported. |
| Large files/maintainability | File sizes are accurate; they do not establish a vulnerability. Extracted small vault/archiving helpers. Larger UI/model refactoring should be incremental with navigation/media regressions preserved. |
| Mixed translation sources | Project uses both Qt tr() and custom bilingual UI. AI errors now use an English base and the selected UI language; new vault security messages also follow that language. Repository-wide Qt .ts migration is separate follow-up work. |
| Missing tests | Existing vault harness already exercised real encryption, password changes and lockouts. Extended it with bad KDF headers, GCM tampering, ciphertext compatibility, oversized sparse input, RAM storage, instance isolation and sibling cleanup. Added dedicated native KDF/path/archive tests and AI download-policy tests to headless CI. |
| Drive OAuth | Restricted scope is required for current full browsing. Documented own-client setup and verification obligations in google-drive-setup.md. |
| Retagging | Previous v1.0.0 rebuild was explicitly requested and AUR's checksum/pkgrel was updated. Published tags should remain immutable: new source version/tag for fixes; pkgrel for packaging-only changes. No tags/releases are changed by this review. |

## Limits

- Linux tmpfs can swap pages. Avoiding ordinary disk-filesystem previews is not
  a guarantee against swap, hibernation, root or another process as the same
  user. Encrypted swap/memory policy belongs to the operating system.
- A crash may leave plaintext in the private RAM directory until filesystem
  cleanup. Normal locking removes plaintext; normal destruction removes the
  directory. Each instance owns a separate directory.
- User-supplied Python environments/local models remain user-managed. Locks
  govern newly provisioned dependencies, not arbitrary existing installations.
- Locks/revisions improve reproducibility and integrity; they do not prove
  third-party software or weights are vulnerability-free.

References: [NIST GCM](https://nvlpubs.nist.gov/nistpubs/Legacy/SP/nistspecialpublication800-38d.pdf),
[OpenSSL Argon2](https://docs.openssl.org/3.6/man7/EVP_KDF-ARGON2/),
[Linux tmpfs](https://docs.kernel.org/filesystems/tmpfs.html),
[pip secure installs](https://pip.pypa.io/en/stable/topics/secure-installs/).
