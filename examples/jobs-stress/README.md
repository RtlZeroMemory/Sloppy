# Jobs Stress

Optional local scheduler workload for enqueue/claim/complete throughput. It
runs through Sloppy Program Mode and uses `data.sqlite` plus `Jobs`.

```sh
sloppy run examples/jobs-stress/main.ts -- ./jobs-stress.db 1000
```
