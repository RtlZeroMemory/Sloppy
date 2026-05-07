# Module Documentation

Module docs document actual implemented internals and must be updated with code.

Every implemented module should have a README in this tree, and `src/<module>/README.md`
may mirror or point to it where useful. These docs are not marketing docs; they are the
working contract for APIs, ownership, invariants, diagnostics, tests, and limitations.

Required module README content:

- purpose;
- current status or status;
- invariants or ownership/lifetime rules.

Add narrower sections such as diagnostics, tests, source docs, non-claims, or
deferred work when they carry useful current information. Do not keep empty
template sections just to satisfy ceremony.
