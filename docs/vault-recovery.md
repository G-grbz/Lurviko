# Optional vault recovery, envelope version 1

## User contract

Recovery is **off by default**, including when creating a vault. Enable it on the
creation screen or in **Private → Security** after unlocking. A generated key is
shown once. In the UI it is a pending preview until you acknowledge safe storage
and choose **Done**. **Cancel** or Escape discards it without changing the stored
settings: recovery stays off on first activation and the previous key remains
valid during rotation. Locking or exiting also discards an unconfirmed preview.
There is no reveal-existing-key action. Store it in a trusted password manager
or offline, separately from the vault. It is a bearer secret: anyone with the
key **and the encrypted vault** can decrypt its contents without the password.
Lurviko does not automatically copy it, upload it, put it in KWallet, or save the
plaintext key in settings, logs or the vault header. Selecting/copying the shown
text is an explicit user action; clipboard managers can retain that copy.

While unlocked, generate a replacement or disable recovery. The new header
contains only the current encrypted recovery envelope. Failed persistence keeps
the previous header/key valid; a displayed but unconfirmed key is not active.
Confirmation also refuses to overwrite intervening header changes from another
instance or operation. Successful recovery
on the lock screen sets a new password, unlocks the vault, resets failed attempts,
and removes the used envelope. Generate a new key afterwards if desired. Invalid
keys share the existing password attempt delay/lockout policy.

### Shown once versus single-use

**Shown once** means the generated key is displayed only at creation; it cannot
be displayed again later. This is separate from how it is used:

| Operation | Effect on the key |
| --- | --- |
| Successful password reset in Lurviko | Single-use for the current vault header: the key is disabled and recovery is turned off. Generate a fresh key in Private → Security to enable recovery again. |
| Failed or cancelled password reset | Does not consume the key. Invalid attempts still count toward the configured lockout policy. |
| Offline export with `recover-vault.py` | Does not consume the key or reset the password. Export can be repeated while the key matches the supplied header. A key disabled in the current header no longer works with that header. |

Single-use is enforced by updating the current header, not by a key that
cryptographically self-destructs. The script cannot restore a removed envelope,
but a saved older header may still accept its old key.

**Revocation cannot erase historical copies.** An older vault/header backup still
accepts its own older recovery key. Existing files use the same master key, so an
attacker retaining an old header and old key can recover that master and decrypt
later ciphertext encrypted with it. Password changes have the same limitation.
An exposed key requires a new vault/master key and re-import of the contents;
simply rotating the envelope is not a remedy for retained hostile copies.
Older Lurviko builds can read the unchanged password/blob format, but may ignore
or mishandle recovery settings. Use the updated build for recovery management.

## Exact interoperable format

All legacy identifiers remain fixed: header magic `g-file-private-vault`, header
version `1`, `GFBLOB01`, `GFTHMB01`, and existing `g-file-private/...` AAD bytes.
Recovery is an optional additive `recovery` object in `vault.json`; absent means
disabled. The vault is normally at
`$XDG_DATA_HOME/Lurviko/Lurviko/.private-vault` (default
`~/.local/share/Lurviko/Lurviko/.private-vault`). Retain the **entire** directory,
including `vault.json` and `objects/`; the key alone cannot recover lost data.

### Printed key

1. Generate **32 random bytes** using OpenSSL's CSPRNG (`RAND_priv_bytes`), never
   a password, UUID, timestamp or PRNG seeded by application code.
2. Encode those 32 bytes as 64 uppercase hexadecimal characters, in eight groups
   of eight. Prefix `LRK1-`.
3. Append `-` plus eight uppercase hex characters representing the first four
   bytes of `SHA256(ASCII("lurviko-recovery-key-v1:") || raw_key)`.
4. Example shape: `LRK1-XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX-CCCCCCCC`.
   The checksum catches typing errors; it is **not** an authentication tag.

Readers accept ASCII hyphens/space/tab/CR/LF as separators and case-insensitive
ASCII hex. After stripping separators, require `LRK1` followed by exactly 72 hex
characters and verify the checksum. Reject non-ASCII lookalikes and oversized
input. The raw 32 bytes are the AES-256 key directly; no password KDF is used
because the recovery key already has 256 bits of random entropy.

### Master-key envelope

The header's `vaultId` and envelope's `id` are nonzero canonical lowercase UUIDs,
with hyphens and without braces. Each generation uses a fresh envelope ID and a
fresh random 12-byte IV. Fields:

| Field in `recovery` | Value |
| --- | --- |
| `version` | JSON number `1` |
| `algorithm` | `aes-256-gcm` |
| `id` | Fresh canonical UUID |
| `iv` | 12 bytes, base64url without padding |
| `wrappedKey` | 32-byte encrypted master key, base64url without padding |
| `tag` | 16-byte GCM authentication tag, base64url without padding |

Encrypt/decrypt the existing 32-byte random master key using AES-256-GCM with
the raw recovery key. AAD is the following exact byte concatenation:

```text
ASCII("lurviko-private/recovery-master-v1") || 0x00 ||
ASCII(vaultId) || 0x00 || ASCII(recovery.id)
```

Require supported version/algorithm, canonical field encodings and exact lengths.
Verify the GCM tag **before** trusting or exposing the master key. Binding both
identifiers prevents moving an envelope to another vault or changing its ID.
Changing/removing the unauthenticated header can still deny access; it cannot
forge the authenticated master wrap/manifest without a key.

### Manifest and object decryption

- With the recovered master key, decrypt `manifestCipher` using `manifestIv`
  (12 bytes), `manifestTag` (16 bytes), and AAD exactly
  `ASCII("g-file-private/manifest-v1")`. All three header fields use unpadded
  base64url. Verify the tag before parsing JSON. The manifest has version `1`
  and an `items` array containing `id`, `parent`, `name`, `dir`, `size`, `mime`,
  `modifiedMs`, and for files, `blob`. Root parent is `root`.
- File ciphertext is `objects/<blob>`; `<blob>` is a canonical UUID without
  an extension. Layout: **8-byte ASCII `GFBLOB01`**, **12-byte IV**, ciphertext,
  **16-byte tag at EOF**. Key is the master key; AAD is
  `ASCII("g-file-private/blob-v1:") || UTF8(item.id)`. Plaintext per object must
  not exceed `2^36 − 32` bytes. Do not publish streamed plaintext until its tag
  verifies. Ordinary stream chunks are not separate authenticated records.
- Optional thumbnail cache `thumbnails/<item.id>.gfthumb` is **8-byte ASCII
  `GFTHMB01`**, **12-byte IV**, **16-byte tag**, then ciphertext. AAD is
  `ASCII("g-file-private/thumb-v1:") || UTF8(item.id)`; plaintext is PNG bytes.
  Thumbnails are dispensable; the reference tool exports original objects only.
- Do not trust decrypted paths blindly: reject separators, `.`/`..`, NULs,
  duplicate IDs/names, missing/cyclic parents, symlinks and existing output files.
  Use a new private output tree with 0700 directories and 0600 files.

## Independent reference tool

`tools/recover-vault.py` uses Python 3 and the independently implemented
`cryptography` AES-GCM primitives, without Qt, Lurviko, KWallet or the original
password. Install that dependency through your distribution (Arch:
`python-cryptography`; Debian/Ubuntu: `python3-cryptography`). Then run:

```sh
python3 tools/recover-vault.py ~/.local/share/Lurviko/Lurviko/.private-vault ~/Recovered-Lurviko
```

The two `Lurviko` path components are intentional: Qt appends the organization
and application names to the data location. If `XDG_DATA_HOME` is customized,
use that data root instead of `~/.local/share`.

The installed tool is also available at
`~/.local/share/Lurviko/tools/recover-vault.py` for a local-prefix installation.

The output directory must not exist. The key is requested with hidden terminal
input; never supply it as a command-line argument or environment variable. Work
from a complete backup with Lurviko closed. The tool reads the vault without
altering headers or encrypted files; export is **not single-use** and does not revoke the key or reset the
password. Repeated exports require separate new output directories. It intentionally needs no Argon2 password-header parameters. Original
file bytes and folder structure are restored; original ownership, timestamps,
permissions and thumbnail caches are not restored. Treat exported content as
untrusted files, not commands to execute.

## Persistence, tests and limits

Recovery changes use a per-vault header-write lock, compare the freshly read
header, and commit using `QSaveFile`; an intervening header update aborts the
change. Ordinary header writes merge the current on-disk recovery envelope so a
stale updated instance cannot restore a revoked key. This is not a general
multi-writer manifest transaction or a defense against malicious same-UID code,
root, rolling back backups, insecure storage, or hardware/power-loss behavior.

Raw pending/derived buffers are explicitly wiped. Qt/QML strings and Python
objects may have unmanaged copies; secure erasure of all memory, swap, crash
dumps, hibernation, screenshots or clipboard history is not promised. A running
unlocked session already has authority to decrypt data and change recovery.
Local attempt limits cannot stop offline use of a copied vault and key.

Native tests cover key parsing/checksums and authenticated envelope tampering;
vault integration covers opt-in creation, one-time consumption, rotation,
disable, lockouts, failed-write rollback, stale instances and unchanged data.
The actual C++ vault is exported by the independent Python tool and its bytes
are compared. Python tests cover permissions, path traversal, symlink sources,
existing output and tampered tags. This is tested implementation, not an external
cryptographic audit or a guarantee of zero vulnerabilities.

References: [OpenSSL random bytes](https://docs.openssl.org/3.4/man3/RAND_bytes/),
[authenticated encryption and tag validation](https://cryptography.io/en/stable/hazmat/primitives/aead/).
