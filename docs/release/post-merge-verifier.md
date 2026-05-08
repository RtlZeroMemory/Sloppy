# Post-Merge Release Distribution Verifier

Use this after the release distribution PR merges. Do not run it as part of the PR unless
explicitly asked.

```text
/goal Verify Sloppy alpha packaging as a fresh contributor and normal user after release-dist PR merge.

Objective:
Test Sloppy from scratch after the release/dist packaging PR has merged. Act as two people:

1. A fresh contributor who wants to build Sloppy from source.
2. A normal user who wants to install Sloppy and build/run an app without building Sloppy from source.

Do not rely on maintainer memory.
Do not use hidden local paths.
Do not edit code unless a fix is needed.
If fixes are needed, open a follow-up PR.

Contributor trial:
- clone the repo fresh;
- run documented bootstrap;
- run doctor;
- configure;
- build;
- test;
- package if documented;
- report every missing dependency or unclear step.

Linux/WSL contributor trial:
- run the Unix flow in WSL/Linux;
- verify Linux x64 source-build path;
- verify Linux package path;
- create external app outside repo;
- build/run it or report exact runtime blocker.

Normal user archive trial:
- start outside repo;
- use generated release archive;
- extract it;
- run sloppy --version;
- run sloppy doctor;
- create external app with sloppy.json and src/main.ts;
- run sloppy build;
- run sloppy run or sloppy run --artifacts;
- report whether compilers/toolchains were required.

Normal user npm trial:
- install from local npm dry-run tarballs;
- run sloppy --version;
- run sloppy doctor;
- create external app;
- build/run app;
- verify no node-gyp/native compile/V8 build/postinstall compile occurred.

Evidence:
- OS/environment;
- commands run;
- PASS/FAIL/SKIPPED/UNAVAILABLE status;
- first error encountered;
- docs gaps;
- hidden dependency/path assumptions;
- whether a normal user could realistically run Sloppy.

Forbidden:
- do not publish npm;
- do not create public GitHub release;
- do not tag alpha;
- do not hide failed lanes;
- do not claim alpha shipped;
- do not claim Linux support unless evidence passes.

Stopping condition:
Open a follow-up PR if fixes are needed, or report verified PASS/limitations if no fixes are needed.
```
