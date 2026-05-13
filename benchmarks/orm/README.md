# ORM Benchmarks

`bench_orm.mjs` is a local microbenchmark for ORM query generation, migration
diffing, and include SQL generation:

```powershell
node benchmarks/orm/bench_orm.mjs
```

The output is machine-readable JSON with local elapsed time and operations per
second. Treat results as local measurement evidence only; do not compare or
publish numbers without recording machine, runtime, command, commit, and
workload details.
