# Git operations

Inside a local working copy, right-click a file, folder or empty space and open
**Git**. Grid, List and Miller Columns share the menu. The navigation indicator
shows the active pane's branch, known commit differences and uncommitted changes:
`main ↑2 ↓1 *`. A check means the known histories match. Missing upstream is
shown explicitly; detached HEAD displays a short commit ID. Full names remain
available in the tooltip and information dialog. Menus and modals follow the
current Turkish/English language and Lurviko or native Qt appearance.

## Selected commits

**Create commit…** distinguishes staged, unstaged, untracked, renamed and deleted
files. Select files and enter a message. Selection starts empty. The selected
files' current working copies are committed, including staged and unstaged
changes; this is file selection, not individual hunk selection. Other staged
and unstaged changes remain intact. Ignored files are not added; conflicts and
untracked nested repository directories cannot be selected here.

A temporary index builds the selected tree. The original branch is updated only
if its previous HEAD still matches, and only selected real-index entries are
updated. Commit hooks are not run because they could add unselected changes.
Configured commit signing is respected; signing errors never silently produce
an unsigned commit. Use another Git client for commit hooks or staged hunks.

**Push after commit** defaults off. If Push or index refresh fails after commit
creation, the local commit is retained and its short ID appears with the error.
The dialog does not silently create another commit on retry.

## Synchronization

**Synchronize repository** identifies the branch's real upstream, Fetches it and
recalculates `HEAD...@{upstream}`. The left count is ahead, the right is behind.

| Local / upstream | Action |
| --- | --- |
| Behind only, clean working tree | Fast-forward only |
| Ahead only | Normal Push to the actual upstream branch |
| Equal | Already up to date; no Pull or Push |
| Both ahead and behind | Stop and offer history comparison |
| Behind with local changes | Stop and preserve local changes |
| No upstream | Stop and require an explicit upstream choice |

**Choose upstream…** lists existing remote-tracking branches. **Set as upstream**
requires explicit confirmation and does not start a transfer. Fetch can populate
that list first. Detached HEAD and branches without a commit cannot synchronize.
Pull is also fast-forward only and never sends local commits. Rejected pushes
refresh remote information when possible while preserving all local commits.

There is no automatic stash, rebase, hard reset, force push or history rewrite.
Branch and HEAD are revalidated before changes. A shared lock prevents concurrent
Lurviko writes to a repository and its worktrees. External Git clients retain
Git's normal locks; external changes can stop an operation and require refresh.

## Networking and resources

The backend uses existing SSH agents and credential helpers. Terminal password
prompts are disabled; authentication failures appear as errors. Credentials are
not stored by Lurviko. Standard HTTPS, SSH, Git and local remotes are supported
without a hosting-provider dependency. Executable custom remote-helper protocols
are rejected. **Open remote repository** maps standard hosting URLs to web links,
removing credentials; local filesystem remotes have no web action.

Nothing continuously Fetches remote changes: counts describe known tracking refs
until a Fetch. GitStatusCache remains read-only and supplies existing file badges.
The separate operation backend uses at most two workers, argument lists and no
shell. Obsolete reads are cancelled. Details are loaded on demand, with a
virtualized list capped at 10,000 changes, 50 history entries and bounded diff
output. Excessive output produces a limit error instead of unbounded allocation.
Explicit writes refresh existing Git and filesystem notification pipelines.

Cancel requests stop the running Git process. A successful local commit is kept
if cancellation arrives later. If a remote accepted an update just before the
connection was interrupted, a subsequent Fetch may be needed to confirm it.

## Validation

`python3 tests/run-git-operations-tests.py` uses isolated working copies and local
bare remotes. It covers selected commits and unrelated staging, renames, deletion,
literal pathspec-like names, signing errors, unborn/detached branches, all sync
cases, upstream selection, nested repositories/worktrees, writer locking,
cancellation, authentication errors, rejected pushes, partial success and Fetch
counts. No test pushes this project's working tree.

`tests/qml/tst_GitManagement.qml` checks menu eligibility in all three views,
dialogs, native theme mode, tabs and split panes. Existing Git badge, navigation,
menu, size and file-operation tests remain relevant. Real hosting-provider login,
interactive signing and disconnected remote filesystems need separate integration
testing.
