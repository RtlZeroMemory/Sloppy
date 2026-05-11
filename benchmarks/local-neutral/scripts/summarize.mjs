export function median(values) {
  const sorted = values.filter((value) => Number.isFinite(value)).sort((a, b) => a - b);
  if (sorted.length === 0) return null;
  const mid = Math.floor(sorted.length / 2);
  return sorted.length % 2 === 0 ? (sorted[mid - 1] + sorted[mid]) / 2 : sorted[mid];
}

export function mean(values) {
  const finite = values.filter((value) => Number.isFinite(value));
  if (finite.length === 0) return null;
  return finite.reduce((sum, value) => sum + value, 0) / finite.length;
}

export function stdev(values) {
  const finite = values.filter((value) => Number.isFinite(value));
  if (finite.length < 2) return 0;
  const avg = mean(finite);
  return Math.sqrt(finite.reduce((sum, value) => sum + ((value - avg) ** 2), 0) / (finite.length - 1));
}

function key(row) {
  return [row.workload, row.connections, row.runtime].join("\u0001");
}

export function summarize(results) {
  const groups = new Map();
  for (const row of results.filter((item) => item.status === "PASS")) {
    const groupKey = key(row);
    if (!groups.has(groupKey)) groups.set(groupKey, []);
    groups.get(groupKey).push(row);
  }
  const summaries = [];
  for (const rows of groups.values()) {
    const first = rows[0];
    const rpsValues = rows.map((row) => row.rps);
    const finiteRpsValues = rpsValues.filter(Number.isFinite);
    summaries.push({
      runtime: first.runtime,
      workload: first.workload,
      connections: first.connections,
      repeats: rows.length,
      medianRps: median(rpsValues),
      meanRps: mean(rpsValues),
      minRps: finiteRpsValues.length === 0 ? null : Math.min(...finiteRpsValues),
      maxRps: finiteRpsValues.length === 0 ? null : Math.max(...finiteRpsValues),
      stdevRps: stdev(rpsValues),
      p50Ms: median(rows.map((row) => row.latencyMs?.p50)),
      p95Ms: median(rows.map((row) => row.latencyMs?.p95)),
      p99Ms: median(rows.map((row) => row.latencyMs?.p99)),
      errors: rows.reduce((sum, row) => sum + Number(row.errors ?? 0), 0),
      non2xx: rows.reduce((sum, row) => sum + Number(row.non2xx ?? 0), 0),
    });
  }
  summaries.sort((a, b) =>
    a.workload.localeCompare(b.workload) ||
    a.connections - b.connections ||
    a.runtime.localeCompare(b.runtime));
  return summaries;
}

export function comparisons(summaries) {
  const byShape = new Map();
  for (const row of summaries) {
    const groupKey = [row.workload, row.connections].join("\u0001");
    if (!byShape.has(groupKey)) byShape.set(groupKey, []);
    byShape.get(groupKey).push(row);
  }
  const rows = [];
  for (const group of byShape.values()) {
    const sorted = [...group].sort((a, b) => (b.medianRps ?? 0) - (a.medianRps ?? 0));
    const sloppy = group.find((row) => row.runtime === "sloppy");
    const deltas = {};
    for (const runtime of ["node", "bun", "deno"]) {
      const other = group.find((row) => row.runtime === runtime);
      deltas[runtime] = sloppy && other && other.medianRps
        ? ((sloppy.medianRps - other.medianRps) / other.medianRps) * 100
        : null;
    }
    rows.push({
      workload: group[0].workload,
      connections: group[0].connections,
      fastestRuntime: sorted[0]?.runtime ?? "",
      sloppyDeltaVs: deltas,
    });
  }
  return rows.sort((a, b) => a.workload.localeCompare(b.workload) || a.connections - b.connections);
}
