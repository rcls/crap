Cvs Remote Access Program
=========================

This is yet another CVS-to-git importer.  Features:

* Handles bizarro real-world messes that occur in CVS archives, such as
  internal inconsistencies in commit ordering and branch structure.
  A [user
  reports](http://stackoverflow.com/questions/881158/is-there-a-migration-tool-from-cvs-to-git#comment-53425427):

> "crap" is the life saver. It's the only thing that has worked for me so far!

* Good performance; attention has been paid to keeping both memory and CPU
  use low.  It does not use an external database other than the VCSs.
* Suitable for incremental use.
* Can access CVS remotely [not recommended for initial imports].
* Merge detection is supported via external scripting.

License
-------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.


Building
--------

I've only built on Linux, but it should not be too hard to get going on
other C99 / Posix platforms.

Just run `make`; the binary is named `crap-clone`.

The only build-time external dependency is `libpipeline` for handling
subprocesses.  `git` needs to be present in the path at runtime.  If running
against a local CVS archive, `cvs` needs to be in the path.

The non-portabilities I know of are are:

* Uses the Linux `asprintf()` and `getopt_long()` functions.
* Use of gcc attributes (format checking, noreturn). `-D__attribute__(X)=`
  should fix that.


Usage
-----

Initial import:

```
mkdir <target>
cd <target>
git init
crap-clone <cvsroot> <cvsrepo>
git gc --aggressive
```
`<cvsroot>` is the usual CVS string to identify a CVS repo:

* For a local repo, just the absolute path, e.g., `/home/cvs`.
* For a pserver repo, the string from `~/.cvspass` should work, e.g.,
  `:pserver:anonymous@example.com:2401/home/cvs`.
* `:ext:<user>@<host>/<path>` for ssh/rsh access.  Note that crap-clone defaults
  to ssh not rsh.

Incremental imports:
----------

Rerun the same crap-clone command in the git repo.

Note that "incremental" is a lie, in that re-running crap-clone re-analyses the
entire CVS history, and recreates the entire git history.  However, the most
expensive part of the import - extracting the file contents - is cached, giving
a huge speed-up over an initial import.


Performance
-----------

crap-clone is written in C and I've attempted to keep memory and cpu use low.
It should be able to cope with even very large CVS repos just fine.  The largest
CVS repo I regularly deal with has:

* several thousand files (up to 100MB each in size).
* many thousands of tags.
 * tens of millions of file-tags.
* many hundreds of branches.
* several tens of thousands of commits.

A from-scratch import of this archive takes around an hour; the bottleneck is
using the `cvs` to retrieve all the versions.  An incremental re-import into an
existing archive takes less than 2 minutes, so long as the version-cache file is
present.


Memory Usage
------------

Because CVS records each file independently, CVS has a non-linear scalability
issue; each tag needs to be processed independently for each file, so we end up
with O(\<number of tags\> * \<number of files\>) memory usage.

This is made worse by the fact that CVS repos often contain large numbers of
tags created in an attempt to cover up deficiencies in CVS.  [E.g., tag every
build, tag source & target of every merge.]  As a result a large CVS repo may
contain hundreds of millions of file-tags.

crap-clone limits the per-file-per-tag memory usage to one pointer (two for
branches), so even a huge CVS repo can be comfortably processed on a machine
with a GB or so of memory.  [I started developing crap-clone on a machine with
192MB memory, so memory usage was a major issue then.  Less so now.]


Bottlenecks
-----------

`crap-clone` uses `cvs` to access the CVS repo, and CVS is the main bottleneck for
the processing:

* CVS does not optimise for extracting multiple versions of the same file.  This
  especially makes the initial import much slower than it could be.  This is
  most noticeable on files that have large numbers of versions.

* CVS handles I/O buffering badly, in particularly doing lots of small writes.
  This seems to be about a 50% overhead on a large `cvs rlog` operation.
  [Writing a dodgy LD_RELOAD library to intercept writes and buffer them gave
  a significant speed up.]

* We do not attempt to transfer file-differences from CVS, resulting in much
  more data than necessary being transferred.  This is not a problem running
  locally, but is an issue for remote access.  [This is due to a bug in CVS when
  accessing multiple versions of the same file.  The sequence is:

  + Retrieve 1.1 of a small file.
  + Ask for diffs between 1.1 and 1.2.  CVS calculates the diffs, but if the
    diffs are larger than the file content, CVS sends the entire file.  But
    *this is the bug* leaves the diff file in the server-side working directory.
  + Now ask for diffs between 1.2 and 1.3.  Because of the previous step, CVS
    thinks it has version 1.2 in the server-side working directory [when it
    actually has a diff].  CVS ends up sending you nonsense.]


Questions & Answers
-------------------

* Why yet another CVS-to-git import?

I started writing this in 2008 when the options were git-cvsimport [which does
not handle the complex messes in the CVS repos I deal with] and git2svn followed
by git-svn-clone [bouncing through svn takes around 24 hours on some of the
repos I deal with.]  If git2cvs had existed back then, I probably wouldn't have
bothered with crap-clone.


* I've just done an import.  Why is my git archive so big?

Run 'git gc --aggressive'.  The pack-files generated by git-fast-import are
often not well compressed.  [git-fast-import could usefully provide a
re-compress-when-closing-the-pack-file option.]


* What is the 'cached-versions' file.

This is the list of git SHA1 identifiers for the CVS file versions, used to
re-use existing versions when doing incremental imports.  It can be given a
different name using the '--cache' option.


* I use character set XXXX.  How do I cope with that?

Like git and CVS, crap-clone treats text as byte-sequences.  This is transparent
to all character sets, but has the down-side of leaving you guessing as to what
character set is in use.  If you can do better than that, patches welcome.


* How is CVS keyword expansion handled?

Currently, all CVS access is done with '-kk'.  That's an arbitrary choice,
patches welcome.


* Can I use CVS with a git working copy?

I use 'git cvsexportcommit' to commit.

The '-e' option to crap-clone adds files to git containing lists of CVS
versions.  It should not be too difficult to write a script that creates CVS
subdirectories from those, if you wanted.


* I did an incremental import and got an error from git-fast-import:

  Not updating XXX (new tip YYY does not contain ZZZ)

The reconstructed CVS history has changed for some reason; because we use
heuristics to reconstruct lots of information that CVS does not maintain
explicitly, this can happen occassionally.  Use the --force option (which gets
passed through to git-fast-import).


* crap-clone [or CVS or git-fast-import] core-dumped / aborted / failed.

Please let me <suckfish@ihug.co.nz> know.  Preferably let me have access to your
CVS repo; in order of preference: rsync access or a tar-ball; ssh or pserver
access to the server; the output of 'cvs rlog' on the module.


* Can I control the usernames / timezones used for the git commits?

Not at present.  Patches welcome.  (One approach would be to upgrade the commit
filter mechanism to do this.)


* Can I import to remote refs rather than local?

Use the --remote option, or for more detailed control, --branch and --tag.


* What is this 'Fix-up commit generated by crap-clone'?

Sometimes crap needs to add a git commit that does not correspond to any commit
in the CVS repository, e.g., because a tag or branch could not be placed exactly
in the parent branch.  The commit comment has a summary of the changes in the
commit.

Be aware that some other CVS-to-git importers will instead attach a tag to an
arbitrary commit, without creating the fix-up, silently corrupting your import.

* How does the commit filter work?

Some information about the commit history is piped to the filter program, and
then the filter may produce output editing the history, such as creating merge
commits.  It's a bit rough and ready; it works for the CVS archives I process
because merges are given source and target tags.  It really needs replacing with
something more flexible.

The lines input to the filter look like:
```
COMMIT <num> <branch>
```
  to identify a commit; <num> is a numeric sequence number; <branch> is the
  branch the commit is on.
```
(BRANCH|TAG) <num> <name>
```
  to identify a branch or tag.  <num> is an arbitrary sequence number.

The lines output from the filter look like:
```
MERGE <target> <source>
```
  add <source> as a parent commit to <target>, making <target> a merge commit.
  Both <source> and <target> are sequence numbers from the filter input.
```
DELETE TAG <num>
```
  remove a tag, <num> is the sequence number from the filter input.



Remote CVS Access
-----------------

crap-clone can access a remote CVS server just fine.  However, note that
crap-clone currently downloads each file version completely, the network traffic
may be huge.  For an initial import over a wide-area network, you are better
off rsync'ing the CVS repo to local disk and running everything locally.

Use the --compress option to compress the network traffic.


Bugs
----

The heuristic for placing late-branching could be improved.  [This is the case
where `cvs tag -b` is used to add new files to an existing branch; we have to
guess which files are branched late and when they were branched.]

The implicit merge from a `cvs import` to the main branch is currently broken.
[I think the code is there, it's just not working.]  Implicit merges to branches
other than 1.x are not supported at all.

The use of CVS the modules file to stich together different parts of the CVS
repo is not supported.  The `-d` command-line option can be used to fake this to some extent.

We just drop "zombie" versions --- where a `,v` file is in the Attic but the last
version on the trunk is not marked as deleted.  This matches the CVS checkout
behaviour, but we could be smarter by keeping the last trunk version and then
faking a delete commit.

The current interface to the merge detection script is not very good.  It
doesn't give enough information, tell lies, and does not cope with the case
where a detected merge requires reordering of commits.
