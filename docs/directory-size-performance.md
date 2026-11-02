# Directory size monitoring and performance validation

Validated on 2 November 2026 with Qt 6.12.0, GCC 16.2.1 and Linux.
These results describe isolated local fixtures, not a benchmark of the user's
entire home directory or of the running desktop application.

## Implementation

- `src/directorysizeindex.{h,cpp}` owns the shared demand queue, bounded memory
  snapshots, cancellation, signals and one nonblocking inotify descriptor.
- `src/directorysizestore.cpp` and `src/directorysizestore_p.h` keeps file metadata and recursive logical
  totals in SQLite. A serial writer performs scans and applies changes; a
  separate single-worker lane reads cached totals and installs visible watches.
  At most two service workers run concurrently. Direct disk Properties and
  footer requests use this service; their former separate background pool is gone.
- `src/directorywatchsubscription.h` adapts DirectoryModel's existing refresh
  signals to this central watcher. `src/directorymodel.{h,cpp}` no longer installs
  a per-file watch for each displayed item.
- `src/contentindexmodel.{h,cpp}` consumes the same visible-directory events.
  Its recursive watcher installation and recurring media-root checks are removed.
  Existing category scanning and indexing behavior remains separate from size
  calculation; this change does not disable a requested category refresh.
- `src/filemetadata_p.h`, `src/directorysizeworker.cpp` and the store use checked
  statx masks with fstatat fallback and no-follow link behavior.
- `src/filepropertiesmanager.{h,cpp}`, `src/fileoperations.cpp` and
  `src/privatevaultmanager.cpp` share totals and invalidate them using actual
  operation results. Restore from Trash includes its destination; unknown
  external restores and vault changes conservatively invalidate cached totals.
- `qml/components/BrowserPane.qml` and `MillerColumnsView.qml` release hidden
  consumers/watches and retain only visible panel/column demand.
- `src/main.cpp` schedules one initial root-size warmup after the first frame.
  The shared writer fills missing records without requiring navigation.
  `src/storagemodel.*` retains device events and manual refresh without the
  five-second polling timer.
- CMake and the headless runner include the size tests. The added Qt SQL module
  is provided by the existing Qt base dependency; no external service is added.

## Work removed and limits

The first startup fills missing totals beneath `/` in the background, excluding
other mounted and virtual filesystems. Later startups reuse an existing root
total instead of validating every cached file. The service does not restore
thousands of descendant watches. Navigating to a cached directory performs an
on-demand directory metadata check; a full recursive verification requires
invalidation or explicit Refresh. No timer periodically scans size trees while idle.

Directory watches are nonrecursive, shared between owners, limited to 64 requested
paths per owner and 256 distinct paths overall, and removed when no longer needed.
Scanning a tree never installs additional watches. Linux events are processed in
bounded batches, and size changes are coalesced over 200 ms. Queue overflow marks
cached state untrusted and schedules controlled validation for visible readers;
hidden roots are invalidated without being scanned. Unused native events
and disk probes do not create a polling loop.

The memory LRU retains 64 snapshots with a 64 KiB child-subtotal budget per
snapshot, plus bounded queues and 32 Properties source wrappers. File-level records
remain on disk rather than in the GUI cache. The persistent database grows with
the number of indexed filesystem entries; the memory limits are not a disk quota.

Leaving the last reader cancels its queued/running calculation cooperatively.
The next requested location takes priority. Cancelled SQL transactions preserve
pending mutation hints as lazy invalidations; they cannot silently lose a file
operation's update. Generation checks reject obsolete view results.

Mounted volumes keep independent totals. Virtual paths such as `/proc`, `/sys`,
`/dev` and `/run` are excluded. An in-place legacy migration detaches historical
virtual/mounted totals without erasing the independent volume records.

## Measurements

The final isolated size-service run reported:

| Check | Observed result |
| --- | --- |
| 1,001 descendant directories | 1 shared visible watch; 0 after all owners leave |
| Navigate away from a 20,000-file scan | New directory ready in 227 ms |
| Initial 30,000-file scan | 653 ms |
| Shared Properties calculation, 80,001 files | 1,327 ms; 1.1108 process CPU seconds |
| Cached deep child while parent verification runs | 50 ms |
| Grow one known file | 1 file metadata lookup, 0 directory listings |
| 1 second with no events | 0.002246 process CPU seconds, no new size jobs |
| Physical I/O during that idle second | 0 read bytes, 0 write bytes |
| Peak RSS of that isolated test process | 33,668 KiB |

`/proc/self/io` supplies physical read/write counters; cached metadata can still
involve system calls without physical reads. RSS is the test process peak, not
Lurviko's whole-window RAM usage. These are single-run observations and depend
on the machine, filesystem cache and competing work.

The previous code allowed up to 8,192 size-tree watches, an additional recursive
category watcher and per-file model watches. The new fixture demonstrates one
shared directory watch independent of its descendant count. No equivalent
whole-application before/after CPU/RAM/I/O profile was captured, so no percentage
improvement or idle desktop CPU guarantee is claimed. Cache reuse is checked by
asserting no extra recursive listings; a production cache hit/miss ratio has not
been profiled.

A read-only 84-row projection of the real legacy cache reproduced the invalid
142,676.72 GiB root total and reduced the projected root to 544.12 GiB by detaching
virtual and mounted scopes, with zero filesystem listings or file stats. This was
a temporary database test, not a scan or modification of the user's live cache,
and is not a promise of the current root size.

## Verification

The dedicated suite passed all 30 checks. It covers initial scans, 30,000+ files, 1,001 subdirectories,
shared/released watches including symlink aliases, rapid navigation cancellation, cancelled-transaction
hints, persistence/restart, mount/virtual scope migration, lazy volume access,
selection deduplication, hard/symbolic links, replaced directories, duplicate
notifications, changes during scanning, corruption, eviction, event loss and idle
CPU/I/O. Native integration checks cover the footer/Properties UI (including trailing-slash paths) and real KIO
copy/move/overwrite/skip/trash/restore/delete with shared totals.

See `tests/directory-size/directory_size_tests.cpp`,
`tests/properties/properties_integration.cpp`, `tests/index/index_scan.cpp` and
`tests/transfers/file_operations_integration.cpp`. The complete regression command
is `.github/scripts/run-headless-tests.sh`.

All 38 commands in that runner were executed in stages. Initial integration
failures exposed category deletion's dependency on the removed recursive watcher
and a trailing-slash mismatch between Properties and normalized service paths;
both were fixed and their affected tests rerun successfully. Tests that previously
counted every metadata-validation transition as a recursive scan now check actual
directory-listing counters and preserved totals.

Final outcomes:

- Application build and `git diff --check`: passed.
- Dedicated size service: 30 passed, 0 failed, including running-selection
  cancellation and invalidating hidden trees without scanning after event loss.
- Properties/permissions UI: passed, including 80,001 files, persistent cache
  reuse, bounded subtotals, nested invalidation and navigation reattachment.
- KIO copy/move/skip/overwrite/trash/restore/delete, archive drag/drop, category
  selections/deletion, session/history, Miller Columns, search, Git and menus:
  passed. Vault, administrator access, memory and security regressions passed.
- Complete QML suite: 546 passed, 0 failed, 1 skipped. The skip requires an
  optional external music collection fixture.
- Additional native desktop appearance checks passed in software and RHI at
  1x/1.5x scales. Separate QML desktop appearance, RHI tab and RHI music runs
  passed 17, 21 and 12 checks respectively.
- Branding, chapters, TMDB, subtitle formatting/readability/translation quality,
  music presentation, system/folder icons and thumbnail regressions passed.
  Translation quality includes 86 pytest checks.

No unresolved test failure remains from this validation. Physical-device and
whole-application profiling limitations below still apply.

## Limits

- Invisible deep descendants are deliberately not continuously watched. Their
  external changes can remain approximate until a relevant index/listing event,
  an application operation, on-demand invalidation or explicit Refresh. Cached
  results are not presented as guaranteed current when coverage is incomplete.
- Category background monitoring likewise receives visible-directory events;
  changes made exclusively in unmonitored deep directories may require category
  Refresh. Existing initial/category scans are not size-service polling.
- Physical USB HDD unplug/replug, sleeping disks, real SMB/SFTP outages, all
  filesystem types and kernel-blocked network I/O have not been benchmarked.
  Cooperative cancellation cannot interrupt an already blocked kernel syscall.
- Temporary archive extraction observation and Git's own metadata/status watches
  retain their existing bounded lifetimes; they are not recursive size watches.
- Full initial enumeration still consumes CPU and I/O while a requested uncached
  tree is being calculated. The idle result does not describe active scanning.
- The initial validation did not replace or restart the installed application.
  A subsequent deployment check is recorded below. No commit, GitHub push or
  release is part of this work.

## Follow-up: repeated navigation and legacy invalidation

A later running-process observation found one pooled worker using about 87% of
one CPU core while the GUI and render threads were idle. The size database held
6,381,142 entries, and cached ancestors including `/` were marked `needs_verify=1`.
The store interpreted that inherited flag as a request to verify every cached
descendant each time a directory was opened. Kernel profiling/ptrace restrictions
prevented capturing a function-level stack of that process.

Named invalidations now persist the affected paths (at most 8,192) instead of
implicitly requiring a whole-tree verification. On demand, those paths update
the shared totals; an ancestor hint does not enumerate a visible child's siblings.
Legacy invalidation flags retain approximate cached values. Explicit Refresh and
lost-event/overflow invalidations still perform full verification when needed.
No existing cache is deleted or automatically rebuilt for this change.

The added regression fails against the previous store: reopening its cached
fixture performs 50 file metadata checks. With the fix, successive parent/child
visits perform zero file checks and zero directory listings. Replaying one
persisted file-growth hint performs one file check and zero directory listings,
and the next visit reuses the updated total. These fixture results are not a
measurement of idle CPU in the user's restarted application.

## Follow-up: first-scan throughput

Initial enumeration now inserts new nondirectory records in groups of at most
64, within the existing cancellable transaction. The batch has at most 832 SQL
parameters. Prepared statements for child lookup and directory totals are reused;
fresh directories skip stale-record deletion because they had no prior children.
File records, hard-link accounting, mount boundaries and cached incremental
updates keep their existing semantics. Worker count and scan throttling rules
are unchanged. In-flight metadata is bounded to one batch, rather than an entire
tree in memory.

An alternating three-run comparison used identical release-optimized helpers,
100,000 distinct sparse regular files in 5,000 directories, and a new SQLite
database on each run. Each directory contained 20 files of 17–36 logical bytes;
all scans returned 2,650,000 bytes, 100,000 files and 5,000 folders. Filesystem
metadata was warm, and the desktop was not otherwise isolated. These figures
compare the store with the preceding navigation fix against the batched store;
they do not compare Lurviko against Filelight.

| Measurement | Before | After |
| --- | --- | --- |
| Wall time, runs 1/2/3 | 3,095 / 2,869 / 2,838 ms | 1,451 / 1,481 / 1,745 ms |
| Median wall time | 2,869 ms | 1,481 ms |
| Median process CPU time | 2.826 s | 1.462 s |
| Peak RSS range | 30,180–30,340 KiB | 30,216–30,328 KiB |

Median time fell by about 48%. This does not establish a speedup for cold HDDs,
network storage or the user's entire home directory. The 31-check size suite
passed after the optimization. An additional regression checks totals and
restart reuse across different batch boundaries, symbolic links, FIFOs, hard-link
growth and deletion. The application also builds successfully.

## Follow-up: startup root fill

The application now requests one initial `/` warmup 1.2 seconds after its UI is
created. It uses the existing serial size writer and its existing mount and
virtual-filesystem exclusions. No visible directory, retained UI consumer or
recursive watch is needed to start this work. An existing root total is reused;
already cached child trees with matching identities/timestamps are also reused
while an incomplete parent is filled. This is a cache warmup, not validation of
all offline changes; overflow invalidations remain available to visible readers.

A foreground size/selection request can cancel the startup batch. Before yielding,
the worker commits completed subtrees and marks open, incompletely enumerated
directories unknown. The pending initial fill resumes after foreground work;
ordinary foreground cancellation continues to roll back its transaction. Errors
do not cause an automatic retry loop. Finishing startup work releases its demand
and leaves no periodic root scan behind.

The 35-check size suite passed with startup coverage. The dedicated cases cover
startup with no visible-directory request, reuse across restart, mounted and
virtual exclusions, preserving partial results while yielding to navigation,
and retaining forced invalidations. The navigation case published its foreground
directory in 303 ms during the background scan. This fixture timing is not a
benchmark of startup on the user's full root filesystem.


## Follow-up: installed binary mismatch

After the user restarted Lurviko and still observed high CPU, the actual process
was inspected. It was running `/home/gkhng/.local/bin/lurviko` with SHA-256
`f0d53ae26f267f2451136bc65bdc30d0e96d81795ad4dd7c5b2db47f92cc0677`,
while the tested workspace build was
`cf788c37045e58374f3c934030be557245cc69c315e47c4681270b8278161c10`.
The old process retained separate inotify sets of 8,192 size-tree watches and
4,096 category watches. One pooled worker consumed about 95% of one CPU core
in the first sample; the GUI thread itself was idle. This is consistent with
about 12% total CPU on the eight-CPU machine, but does not identify an exact
function-level hot spot. Kernel perf permissions prevented sampling.

The tested executable was installed atomically at the existing user-local path
with a previous-binary backup in `/tmp/lurviko-before-demand-driven-install`.
The installed and built files now have identical SHA-256 hashes. The existing
window was not forcibly closed. The user restarted normally, and PID 488535
was verified to run the matching new hash. The former 8,192/4,096 watch sets
were absent; the remaining five active watches belonged to another existing
watcher, with zero watches in the new central service while Explore was open.

After startup settled, a ten-second `pidstat -u -d` observation reported 0.10%
CPU averaged on the one-core process scale (100% means one CPU), and 0 physical
read/write throughput. An independent six-second `/proc` delta reported zero
CPU ticks and zero physical read/write bytes. This is an observation of the
actual new application, not a universal idle guarantee or a controlled hardware
benchmark. Startup work initially consumed more CPU before it completed.
Earlier isolated measurements must not be treated as measurements of the old
installed process.
