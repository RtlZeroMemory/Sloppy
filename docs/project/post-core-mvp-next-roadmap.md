# Post-Core MVP Next Roadmap Proposal

Status: proposal only. Do not create GitHub EPICs from this document without owner
approval.

## 1. Core MVP Status

| Area | Status | Notes |
| --- | --- | --- |
| Compiler -> Plan/artifacts | complete/proven | Supported subset emits deterministic artifacts and source maps. |
| V8 runtime execution | complete/proven for scoped path | V8-gated artifact execution, handlers, request context, direct async microtask settlement, and SQLite bridge paths exist. |
| HTTP backend semantics | complete/proven for MVP | Parser/body/response/dispatch/shutdown/admission semantics exist for bounded non-production paths. |
| Libuv localhost transport | complete/proven for MVP | One request per connection, close-after-response, bounded read/write/timeout/shutdown. |
| SQLite users API proof | complete/proven | Source-built users API runs over localhost TCP through V8 and capability-gated SQLite. |
| Provider execution/offload | partial | Native executor exists; current SQLite bridge is not yet routed through it. |
| Capability enforcement | complete/proven for integrated paths | SQLite bridge and provider executor enforce before provider work; filesystem/network remain skeletons. |
| Conformance/package evidence | complete/proven for current lanes | ENGINE-19 evidence lanes and package smoke exist; optional V8/package/live lanes must stay separate. |
| Source-input run | missing | Explicit two-step artifact workflow remains current. |
| Public alpha | blocked | Needs canonical docs, executable examples, source-input decision, ergonomics, and package/platform story. |

## 2. Next Tracks

### Track A - Framework/App Layer

Possible EPICs:

- configuration system completion;
- services/DI completion;
- request binding and validation;
- result/response model completion;
- Plan-driven OpenAPI;
- source-input run/dev loop;
- examples hardening.

### Track B - HTTP Post-MVP Transport

Possible EPICs:

- HTTP/1.1 keep-alive and sequential request loop;
- idle timeout and max requests per connection;
- chunked request decoding;
- streaming response writer;
- TLS and reverse proxy awareness later;
- WebSockets later;
- HTTP/2 research/implementation later.

### Track C - Strong Plan Strategic Layer

Possible EPICs:

- typed Plan graph model;
- route/body/provider/capability/response-shape graph;
- Plan-driven validation/audit/doctor;
- Plan-driven OpenAPI;
- future native JSON serialization/fast-path candidates;
- multi-isolate/route partitioning later as research.

### Track D - Provider Expansion Later

Possible EPICs:

- route SQLite bridge work through provider executors before scalable provider claims;
- PostgreSQL JS bridge;
- SQL Server JS bridge;
- provider-specific cancellation;
- pooling strategy;
- live provider conformance.

## 3. Public Alpha Blockers

- Docs are cleaned and canonical.
- Examples are honest and executable.
- Conformance evidence is stable and split by default, V8-gated, transport, SQLite,
  package, live-provider, stress, and benchmark lanes.
- Source-input run decision is made.
- Framework ergonomics do not require normal app authors to hand-write capability blocks.
- Platform/package story is clear.
- No production HTTP, public performance, Node/npm, or package-manager claims are implied.

## 4. Recommended Next EPIC Creation Order

1. Source-of-truth/framework app-layer design docs/issues.
2. Source-input run/dev loop if needed.
3. Request binding/validation/config.
4. Plan-driven OpenAPI/doctor.
5. HTTP keep-alive/streaming.
6. Provider expansion later.
