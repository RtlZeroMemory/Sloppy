# Fuzz And Property Tests

`tests/fuzz` owns the TEST-PLATFORM-01 fuzz/property lane.

The lane has two layers:

- default-safe seed replay: standalone executables run the committed corpus through CTest
  with labels `fuzz;property;seed-replay`;
- mandatory libFuzzer seed replay: CI builds the `windows-libfuzzer` preset and runs the
  deterministic `fuzz` CTest label;
- opt-in libFuzzer mutation: libFuzzer executables may run bounded mutation campaigns
  against reviewed corpora.

Current targets:

| Target | Contract |
| --- | --- |
| `fuzz_plan_parse` | Plan parser accepts valid JSON, rejects malformed input with stable diagnostics, and never crashes on arbitrary bytes. |
| `fuzz_route_pattern` | Route parser/matcher handles invalid patterns, parameter routes, and embedded NUL bytes without memory errors. |
| `fuzz_http_request` | HTTP request parser handles malformed heads, bounded limits, and partial/malformed bytes without crashes. |
| `fuzz_diagnostics_render` | Diagnostic renderers handle arbitrary text/path/source inputs and redaction never emits secret-marked values. |
| `fuzz_memory_primitives` | Arena, checked-size, string/byte scan, and builder helpers preserve invariants for deterministic memory seeds. |

Default seed replay:

```powershell
ctest --test-dir build\windows-dev -L fuzz --output-on-failure
```

Seed replay is the default-safe fuzz/property lane. It proves the checked-in corpus stays
bounded and crash-free. The `windows-libfuzzer` preset repeats the seed replay with
libFuzzer instrumentation in mandatory CI. Mutation runs are separate optional evidence and
must name the target, corpus, toolchain, duration, and result explicitly.

Opt-in libFuzzer:

```powershell
.\tools\windows\dev.ps1 configure -Preset windows-libfuzzer
.\tools\windows\dev.ps1 build -Preset windows-libfuzzer
.\build\windows-libfuzzer\fuzz_plan_parse_libfuzzer.exe tests\fuzz\corpus\plan -max_total_time=60
.\build\windows-libfuzzer\fuzz_memory_primitives_libfuzzer.exe tests\fuzz\corpus\memory-primitives -max_total_time=60
```

Long fuzzing is optional evidence. Report it as `fuzz/property`, not default non-V8. A
skipped or unavailable libFuzzer toolchain is not pass evidence.
