# Local security review

Checked against the source prepared for Lurviko v1.2.0 (including the v1.1.0 review and fixes initially prepared for v1.1.1).

| Report | Finding/action |
| --- | --- |
| Optional recovery keys | Opt-in 256-bit random bearer keys wrap the existing master key with AES-256-GCM and vault/envelope-bound AAD. Keys are shown once, never persisted plaintext, and can be replaced/disabled while unlocked. UI previews require explicit safe-storage confirmation; Cancel/Escape preserves the old envelope or disabled state. Successful in-app password reset removes the used envelope (single-use for the current header); the read-only offline export tool deliberately keeps it and allows repeated exports. UI notices and the tool help distinguish single-use from one-time display. Atomic locked compare-and-swap protects replacement; ordinary updated-instance writes preserve current revocation. Native, actual-vault and independent Python export tests cover invalid/tampered keys, one-time display, rotation, failed-write rollback and path safety. See [format and limits](vault-recovery.md). |
| Recursive local filesystem boundary | Confirmed: local traversal did not check st_dev. Check the selected root's device and each entry/opened child; skip other devices without modifying their contents or mount root. Report the skipped count in progress/completion. Native fd/syscall doubles exercise a device change between path stat and open without requiring a real mount/root. No cross-filesystem opt-in is added in this version. |
| Special permission bits | Confirmed intentional preservation of existing setuid/setgid/sticky bits; ordinary rwx controls cannot set/clear them. Added code comments and a bilingual Properties hint. Existing ownership changes may clear privilege bits and they are not restored. Native regression verifies unchanged special bits without ownership changes and unchanged file content. |
| Unauthenticated Argon2id header parameters | Confirmed in derivePasswordKey: parameter validation precedes derivation and GCM verification, and the legacy wrapping AAD omits the header parameters. No format/code change; documented a future versioned unlock/rewrap/rollback design in [vault-kdf-upgrade-design.md](vault-kdf-upgrade-design.md). Bounds still limit unauthenticated resource requests. |
| Crash plaintext residue | Confirmed: random session directories were removed only on normal destruction. Each new session now holds an O_CLOEXEC flock lock; startup sweeps preferred runtime and /dev/shm session directories, plus legacy lurviko-private in runtime and the old temporary parent. Owner/fd/inode checks and nonblocking locks protect live sessions; descriptor-based deletion rejects symlinks and mount crossings. Native tests cover live/crashed processes, lockless orphans, legacy names, symlinks, simulated foreign ownership and instance isolation. |
| Recursive administrator chmod/chown | Confirmed in kio-admin 26.08.1 and KIO 6.30.0 source: the final chmod/chown are path-based. Before recursive administrator jobs, perform local fd/no-follow tree checks; reject directories owned by neither root nor the target owner, group/other-write directories, and unreadable trees. Fake administrator regression verifies an unsafe symlink tree cannot dispatch a job or change an external target. See [source evidence and helper design](privileged-access-design.md). Preflight is a mitigation, not inode-pinned privileged execution. |
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

- Recovery previews are staged in memory. Cancel/Escape/lock retain the previous
  on-disk recovery settings; activation requires confirming safe storage. A
  compare-and-swap header write prevents overwriting intervening changes. The
  displayed preview is not valid until confirmation succeeds.

- Recovery keys grant access equivalent to the master key. Revocation applies to
  the current header; copied old headers/keys retain access to their master key,
  including later objects encrypted with that same master. Qt/QML/Python strings,
  clipboard managers, swap and crash dumps cannot be guaranteed securely erased.
  The UI is opt-in and displays keys only once; see [the recovery contract](vault-recovery.md).

- Recursive administrator operations still use path-based KIO after a conservative
  preflight. Trusted-owner mutations, mutable ancestor paths, non-POSIX ACLs,
  hard links and mount changes are not solved atomically. Unreadable or
  group/other-writable trees are refused. A privileged fd helper remains design
  work; see [the proposal](privileged-access-design.md).

- Local recursive permission changes stay on the root st_dev by default. Same-device
  bind mounts are not distinguished by st_dev; skipping such mounts would require
  a further mount-ID/openat2 policy. Skipped counts describe entries/subtree roots,
  not the number of unseen files inside them. There is no crossing opt-in.
- Special permission bits remain outside the ordinary rwx editor. Ownership
  changes can clear setuid/setgid as before; this update does not restore them.
- Legacy Argon2id parameters are bounded but are not authenticated header fields.
  Requests within those bounds can still consume KDF resources before a wrong
  key fails GCM verification. Rehash-on-unlock is design work, not implemented.
- Linux tmpfs can swap pages. Avoiding ordinary disk-filesystem previews is not
  a guarantee against swap, hibernation, root or another process as the same
  user. Encrypted swap/memory policy belongs to the operating system.
- A crash can leave plaintext until the next application startup (or operating
  system cleanup). Startup now removes owned, unlocked orphan sessions, including
  legacy runtime/temp directories. Normal locking clears plaintext while retaining
  the session lock; destruction removes that instance's directory. Live sessions
  with locks are skipped. Older releases did not hold locks: their live lockless
  directories cannot be distinguished from orphans, so close old versions before
  upgrading. Cleanup failures (permissions, mount crossings or unavailable
  openat2 support) leave the candidate in place rather than follow unsafe paths.
  Advisory locks do not defend against another malicious process with the same UID.
- User-supplied Python environments/local models remain user-managed. Locks
  govern newly provisioned dependencies, not arbitrary existing installations.
- Locks/revisions improve reproducibility and integrity; they do not prove
  third-party software or weights are vulnerability-free.

References: [NIST GCM](https://nvlpubs.nist.gov/nistpubs/Legacy/SP/nistspecialpublication800-38d.pdf),
[OpenSSL Argon2](https://docs.openssl.org/3.6/man7/EVP_KDF-ARGON2/),
[Linux tmpfs](https://docs.kernel.org/filesystems/tmpfs.html),
[pip secure installs](https://pip.pypa.io/en/stable/topics/secure-installs/).

## v1.1.1 validation (2026-10-11)

- `cmake --build build -j4`: successful Release build.
- `python3 tests/run-security-tests.py`: 10 QtTest cases passed, no failures or
  skips. Foreign ownership and a changed device are simulated through test-only
  syscall wrappers; the administrator dispatch is a test double, not a root job.
- `bash .github/scripts/run-headless-tests.sh`: every command passed, including
  the real vault integration/crash recovery harness, 42 QML cases, 10 system-icon
  cases and 86 translation-quality cases, plus the UI, uninstall, download-policy,
  branding, chapter and subtitle guards. The script now includes vault integration.
- `python3 tests/run-properties-tests.py`: passed native permission/content
  checks and the actual Properties UI, including the special-bit explanation.
  The scroll assertion waits for its animation instead of a fixed short delay.
- The initial sandbox run blocked local ownership/media operations. The complete
  headless suite and Properties harness subsequently passed outside that sandbox;
  no test remained unrun. A test helper signature error was corrected before the
  final successful Properties run.
- Compared vault magic and legacy AAD expressions against v1.1.0: unchanged.
  `makepkg --printsrcinfo` matches the prepared metadata (apart from its checksum
  reminder comment), and neither published tag was modified.

## Recovery follow-up validation (2026-10-11)

- Security runner: 12 passed, zero failures/skips, including recovery key encoding
  and authenticated envelope tampering.
- Actual-vault backend and Wayland UI harnesses passed recovery creation, consent,
  one-time display, rotation, disable, failed-write rollback, retry protection,
  stale-instance writes and password reset with byte-identical decrypted data.
- Independent Python recovery exported the real C++ fixture successfully without
  modifying its header. Five Python tool tests passed permissions, traversal,
  source symlinks, existing destinations and damaged authentication tags.
- All headless commands passed, including 42 QML cases, 10 system-icon cases and
  86 translation-quality cases. Test dependency setup includes python-cryptography.
- Legacy magic/AAD values and header version remain unchanged; recovery is an
  optional header extension with its own authenticated envelope version.

## v1.2.0 release preparation

The source and AUR version fields are prepared for 1.2.0. Published v1.0.0 and
v1.1.0 tags remain unchanged. AUR source checksums are intentionally absent:
**release sonrası güncellenecek** (update after the real release archive exists).
Do not publish/build the AUR update until the actual archive's SHA-256 is recorded
and .SRCINFO is regenerated. An empty checksum array is not a SKIP checksum.
