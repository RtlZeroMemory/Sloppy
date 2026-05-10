# Fuzz And Property Tests

`tests/fuzz` owns the fuzz/property lane.

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
| `fuzz_http_query` | HTTP query parsing handles repeated keys, percent decoding, invalid escapes, and capacity limits without crashes. |
| `fuzz_http2_frame` | HTTP/2 frame parsing handles invalid lengths, SETTINGS, WINDOW_UPDATE, CONTINUATION, DATA-before-headers, and GOAWAY edge seeds. |
| `fuzz_http2_hpack` | HPACK/header validation handles invalid indexes, pseudo-header order, duplicate pseudo-headers, and uppercase regular headers. |
| `fuzz_http2_session` | HTTP/2 session seed replay covers malformed prefaces, DATA-before-HEADERS, unknown RST streams, GOAWAY, and continuation ordering. |
| `fuzz_diagnostics_render` | Diagnostic renderers handle arbitrary text/path/source inputs and redaction never emits secret-marked values. |
| `fuzz_memory_primitives` | Arena, checked-size, string/byte scan, and builder helpers preserve invariants for deterministic memory seeds. |
| `js_fuzz_targets.mjs` | JavaScript randomized/property coverage for config, route plans, headers, query strings, percent decoding, logging redaction, package manifests, route tables, gated features, HTTP/1 and HTTP/2 client options, result descriptors, worker queues, and stdlib import shapes. |
| `run_property_tests.mjs` | Bootstrap stdlib properties for codec, Results/ProblemDetails, time, HttpClient option validation, workers, logging, and config. |

Default seed replay:

```powershell
ctest --test-dir build\windows-dev -L fuzz --output-on-failure
```

Wrapper:

```powershell
.\tools\windows\fuzz.ps1 -All -Iterations 1000 -Seed 12345
.\tools\windows\fuzz.ps1 -Target http-query -Iterations 10000 -Seed 12345
```

Seed replay is the default-safe fuzz/property lane. It validates that the
checked-in corpus stays bounded and crash-free. The `windows-libfuzzer` preset repeats the seed replay with
libFuzzer instrumentation in mandatory CI. Mutation runs are separate optional evidence and
must name the target, corpus, toolchain, duration, and result explicitly.

Opt-in libFuzzer:

```powershell
.\tools\windows\dev.ps1 configure -Preset windows-libfuzzer
.\tools\windows\dev.ps1 build -Preset windows-libfuzzer
.\build\windows-libfuzzer\fuzz_plan_parse_libfuzzer.exe tests\fuzz\corpus\plan -max_total_time=60
.\build\windows-libfuzzer\fuzz_memory_primitives_libfuzzer.exe tests\fuzz\corpus\memory-primitives -max_total_time=60
```

JavaScript randomized/property replay:

```powershell
node tests\fuzz\js_fuzz_targets.mjs --all --iterations 1000 --seed 12345
node tests\bootstrap\property\run_property_tests.mjs --iterations 1000 --seed 12345
```

Both JavaScript runners write failure artifacts under `artifacts/fuzz/failures`
or `artifacts/property/failures` and include seed, target, and iteration in the
failure message.

Long fuzzing is optional. Report it as `fuzz/property`, not default non-V8. A
skipped or unavailable libFuzzer toolchain gets its own status.
