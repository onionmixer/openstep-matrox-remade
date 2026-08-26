# R6 — validating the installed bundle's ownership and permissions

Status: **plan.** No code changed.

## Why

R5 made the installer preserve the machine's configuration, and its
validation covers the one hazard this project has actually been bitten by:
driverLoader **abandons the whole driver configuration** if any file in a
bundle is group- or other-writable (`file ... is writable; aborting`,
`openstep-spacesaver2ps2/doc/openstep-ps2-internals.md:263`).

Cross-review pointed out that the validation stops there and trusts
`chown`'s exit status for everything else.  It does not establish that the
files really are root-owned, that they are readable, that nothing is setuid,
or that the bundle contains only ordinary files and directories.  None of
those is the known hazard, but all of them are cheap to check and one of
them -- an unreadable or non-regular file -- would break the driver in a way
the current checks would pass.

## What will be enforced, and why each

Every predicate below was run against the installed bundle before being
written down.  All of them are supported by this `find` and all currently
match nothing.

| check | `find` predicate | why |
| --- | --- | --- |
| group-writable | `-perm -020` | the known driverLoader refusal (already enforced) |
| other-writable | `-perm -002` | the same (already enforced) |
| not owned by root | `! -user root` | `chown -R root` is trusted today; this measures the result |
| setuid | `-perm -4000` | a driver bundle has no business carrying one |
| setgid | `-perm -2000` | the same |
| sticky | `-perm -1000` | the same |
| symlink | `-type l` | insurance, not a live guard -- see below |
| not a file or directory | `! -type f ! -type d` | the same |
| directory nobody can read or enter | `-type d ! -perm -555` | an untraversable directory breaks the driver while passing every existing check.  **Not `-700`**: that demands owner WRITE, which would refuse a legitimate read-only 555 resource directory -- computed, `0555 & 0700 != 0700` |
| file nobody can read | `-type f ! -perm -444` | the same |
| the inspector not executable | `! -perm -555` on that one path | Configure loads and runs it; a mode of 400 or 600 passes every other check here and cannot work |
| not owned by group wheel | `! -group wheel` | see below -- this was almost dropped for the wrong reason |
| a regular file with more than one link | `-type f -links +1` | a clean-bundle invariant; `cp -r` breaks links, so anything left is something else's doing |

The three access checks are `-555`/`-444` rather than exact modes because the
bundle we ship is not one mode: verified with python against the measured
modes, `-555` for directories and the inspector and `-444` for files accepts
every one of directories 755, tables 444, files 644 and inspector 755, and
refuses anything the owner or the world cannot read.

### Two of those cannot fire today, and the plan should say so

Measured on the target: **`cp -r` DEREFERENCES symlinks.**  A symlink in the
build tree becomes an ordinary file in the candidate --

```
source:        lrwxrwxrwx  link -> real
after cp -r:   -rw-r--r--  link            (6 bytes, the target's content)
find -type l in the copy: nothing
```

-- and it **breaks hard links** as well (link count 2 in the source, 1 in the
copy).  So the candidate is ordinary files and directories by construction,
and `-type l` and `! -type f ! -type d` cannot fire in normal operation.

They are kept anyway, because they are free and because the thing that makes
them unable to fire is a property of `cp -r`, not of this script: a future
change to a copy method that preserves links would make them live.  But the
plan must not pretend they are guarding anything today, and **the test for
them has to be honest about what it tests** -- see the test plan.

Hard links need no check of their own for the same reason.

## What will NOT be enforced, and why not

- **Exact modes.**  Cross-review suggested the sibling installer's stated
  policy of 755 directories, 644 files, 755 executables.  Measured, the
  installed bundle is not that and should not be: `Default.table` and
  `Instance0.table` are **444**, deliberately read-only, and a rule of
  "644 or 755" rejects the working bundle.  The sibling states that policy
  in a comment and does not enforce it either (`tools/nx-install-driver.sh:103`
  says it; line 120 runs only `chmod -R go-w`).  Enforcing a mode policy
  would mean inventing one; the checks above enforce the properties that
  matter without pinning bits that legitimately differ.
- ~~**Group ownership.**~~  **This was wrong and is now enforced.**  I wrote
  that this `find` could not express it, having only tried `! -group 0`.
  It wants a NAME, and the name is measurable: `/etc/group` gives
  `wheel:*:0:root,me`, the installed bundle is `root:wheel`,
  `find ! -group wheel -print` works and matches nothing, and this `chown`
  accepts `root.wheel`.  All four checked on the target.  So the installer
  will `chown -R root.wheel` and verify it.  "The tool cannot do it" is a
  claim that needed one more command before it was written down.

## How

The two existing writable checks already use the shape this needs -- run
`find`, check its own exit status, then test whether the output file is
non-empty -- because a `find` that fails must not read as "nothing found".
The new checks reuse it, so the addition is one loop over a table of
(predicate, message) rather than ten copies of the same six lines.

Note for whoever edits it: `printf` does not exist on this system, and this
shell has no `${var##pattern}`; both were tried.  `echo` and `basename` are
what there is.

## The tests I had written would have passed for the wrong reason

Validation runs AFTER `chown -R root` and `chmod -R go-w`.  So a source
fixture that is group-writable, other-writable or non-root-owned is
**normalised before validation ever sees it**, and the install succeeds.
Tests 1-3 as first written would have reported "not refused" and I would
have read that as a broken check rather than as a test aimed at the wrong
place.

Two consequences:

- **Source structural preflight.**  `cp -r` dereferences symlinks, so a
  symlink in the build tree becomes an ordinary file before any check.  The
  only place to catch it is BEFORE the copy:
  `find $SRC ! -type f ! -type d -print`, which also catches FIFOs, devices
  and sockets -- and `cp` can block or fail on those.  Verified: the current
  build tree matches nothing.
- **The post-normalisation checks are tested by corrupting the CANDIDATE**,
  not the source.  That needs the validation to be reachable on demand, so
  it goes in a function the test can call after deliberately breaking the
  candidate.

## Test plan

No reboot.

1. Source preflight: put a FIFO in a copy of the build tree -> refused
   before the copy.
2. Candidate validation, called directly on a deliberately broken copy:
   group-writable, other-writable, non-root owner, non-wheel group, setuid,
   setgid, sticky, a 000 file, a 644 directory, an inspector without the
   execute bit, a hard-linked file, a zero-byte instance table, an
   `InstanceFoo.table` -> each refused with its own message.
3. A 555 directory and a 444 file -> **accepted**, because the policy is
   properties and not exact modes.
4. A symlink in the build tree -> refused by the source preflight, which is
   the only place that can see it.
5. An untouched bundle -> installs, exit 0, instance table byte-identical.
6. The nine R5 tests re-run unchanged, as a regression.

Test 5 is the one that matters most: a validation that rejects the bundle
the project actually ships would be worse than none.

## Four defects in the installer itself, found in the same review

None of these is about permissions, and all of them are in scope because
they are in the lines this change touches:

1. **"Every step is checked" is not true.**  `rm -rf $C` (`:71`),
   `rm -f $C/Instance*.table` (`:81`) and `rm -f $C/.lastBuildTime`
   (`:109`) are unchecked, and a failure leaves stale candidate contents.
2. **`/tmp/osmga-w.list` is a predictable temp file in a root script.**  An
   unprivileged user can pre-create it as a symlink and have the
   installer's redirection truncate something else.  It moves beside `$C`,
   in the root-owned driver directory, and is removed in `fail()`.
3. **The first-install instance check accepts anything.**  The loop at
   `:101` takes any `Instance*.table` -- `InstanceFoo.table`,
   `Instance100.table`, a zero-byte one.  The name check runs only on the
   import path.  Every candidate instance table must pass the same name
   rule and be non-empty, on both paths.
4. **The pre-cutover re-comparison cannot see a table ADDED** after the
   snapshot.  It re-enumerates the allowed set immediately before the
   renames instead.

## Result, 2026-08-26

Implemented and measured.  Seventeen validation cases plus a source
preflight plus the seven R5 regressions, none needing a reboot.

**Refused, each with its own message:** group-writable, other-writable,
setuid, setgid, sticky, a file nobody can read, a directory nobody can
enter, an inspector without the execute bit, a non-root owner, a non-wheel
group, a zero-byte instance table, `InstanceFoo.table`, a symlink, a
hard-linked file, and -- caught before the copy -- a symlink in the build
tree.

**Accepted:** a 555 directory, a 444 file, and the untouched shipping
bundle.  That last one is the test that matters: a validation that rejects
what the project ships would be worse than none.

**R5 regressions all still pass**: preservation byte-for-byte (sum 18624),
legacy `BW:4` with no `Gray Levels`, an older table keeping its absent
keys, a second instance table, a machine with only `Instance1` not gaining
an `Instance0`, and a zero-byte relocatable refused.

### Three things the implementation found that the plan had not

1. **`refuse_if_any` truncated its predicate.**  Written as `$2 $3 $4 $5 $6`
   it dropped the sixth word of `! -type f ! -type d`, and the mangled
   predicate matched every directory -- so the first run refused the
   shipping bundle for containing directories.  It takes `shift` and `"$@"`
   now.  A helper that silently drops arguments is worse than no helper.
2. **The symlink case was refused for the wrong reason.**  A symlink's own
   mode is `lrwxrwxrwx`, so the group-writable check fired first and
   reported "group-writable in the bundle" -- true, and it would send
   someone hunting for a `chmod` that is not there.  The structural checks
   now run before the mode checks so the specific message wins.
3. **The FIFO case cannot be exercised here.**  This `mknod` takes only
   `b`/`c`: `usage: mknod name b/c major minor`.  The predicate that would
   catch a FIFO is the same one that catches the symlink, and that is
   tested, but the FIFO itself is untested and is recorded as such rather
   than claimed.

### And one claim of mine that was wrong

The plan first said this `find` could not express group ownership, having
only tried `! -group 0`.  It wants a NAME.  `/etc/group` gives
`wheel:*:0:root,me`, the bundle is `root:wheel`, `find ! -group wheel`
works, and this `chown` takes `root.wheel`.  All four measured.  The
installer now normalises and verifies the group.  "The tool cannot do it"
needed one more command before it was written down.
