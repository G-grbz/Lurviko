# Future Argon2id upgrades on unlock (design only)

The current vault header stores Argon2id parameters outside authenticated data.
`derivePasswordKey` validates integer ranges before deriving a key, but that
validation is a resource bound, not authentication of those parameters. The
wrapped master key still needs its AES-GCM tag to verify before the vault opens.
This review does not change any on-disk magic, version or legacy AAD bytes.

A future upgrade should use an explicit new header version, with bounded and
validated KDF parameters authenticated as part of the new wrapping scheme. Keep
a version-specific legacy reader that derives using exactly the old stored
parameters and verifies the existing master-key wrap. Reject invalid parameters
rather than clamp them, because clamping derives a different key.

After a successful unlock, compare the authenticated/validated old settings to
a tested resource policy. If stronger settings are needed, retain the password
only long enough to derive the replacement password key with a fresh random salt. Rewrap the existing
random master key with a fresh IV and the new version's authenticated header
fields; do not decrypt and rewrite every file or thumbnail. Legacy blob,
thumbnail and manifest AAD identifiers remain fixed for existing objects.

Write the complete replacement header to a private temporary file, flush it,
verify that the new wrap can be opened and that it yields the same master key,
and atomically replace the original header. Serialize header updates with
password changes and other instances. Ensure file/directory durability before
reporting the migration complete. Keep a recoverable original until the new
header is verified; never leave two ambiguous authoritative headers.

If derivation, allocation, cancellation, disk writes or verification fail,
leave the old header authoritative and keep the unlock result usable. An
interrupted upgrade must reopen with the original password and old parameters.
Wipe password/derived-key buffers on every exit. Test legacy unlock, weak/strong
parameter policies, wrong passwords, bounded allocations, concurrent changes,
crashes at each persistence step and rollback. This needs a separate format and
compatibility review before implementation; there is no rehash code in v1.1.1.
