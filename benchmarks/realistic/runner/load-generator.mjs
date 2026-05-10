import http from "node:http";
import https from "node:https";
import { performance } from "node:perf_hooks";

function makeRandom(seed) {
  let state = seed >>> 0;
  return () => {
    state = (1664525 * state + 1013904223) >>> 0;
    return state / 0x100000000;
  };
}

function percentile(sorted, pct) {
  if (sorted.length === 0) {
    return null;
  }
  const index = Math.min(sorted.length - 1, Math.max(0, Math.ceil((pct / 100) * sorted.length) - 1));
  return sorted[index];
}

function weightedPicker(requests, seed) {
  const weighted = requests.map((request) => ({ ...request, weight: request.weight ?? 1 }));
  const total = weighted.reduce((sum, request) => sum + request.weight, 0);
  const random = makeRandom(seed);
  return () => {
    const value = random() * total;
    let cursor = 0;
    for (const request of weighted) {
      cursor += request.weight;
      if (value <= cursor) {
        return request;
      }
    }
    return weighted[weighted.length - 1];
  };
}

export function summarizeLoad(samples, durationSeconds) {
  const latencies = samples.latencies.slice().sort((a, b) => a - b);
  const totalRequests = samples.totalRequests;
  const totalLatency = samples.latencies.reduce((sum, value) => sum + value, 0);
  return {
    requestsPerSecond: durationSeconds > 0 ? totalRequests / durationSeconds : 0,
    latency: {
      avgMs: totalRequests > 0 ? totalLatency / totalRequests : null,
      p50Ms: percentile(latencies, 50),
      p75Ms: percentile(latencies, 75),
      p90Ms: percentile(latencies, 90),
      p95Ms: percentile(latencies, 95),
      p99Ms: percentile(latencies, 99),
      maxMs: latencies.length > 0 ? latencies[latencies.length - 1] : null,
    },
    transfer: {
      bytesPerSecond: durationSeconds > 0 ? samples.bytes / durationSeconds : 0,
      totalRequests,
      errors: samples.errors,
      timeouts: samples.timeouts,
      non2xx: samples.non2xx,
    },
  };
}

export function singleRequest(baseUrl, requestSpec, options = {}) {
  const target = new URL(requestSpec.path, baseUrl);
  const client = target.protocol === "https:" ? https : http;
  const started = performance.now();
  const body = requestSpec.body ?? null;
  const headers = { ...(requestSpec.headers ?? {}) };
  if (body !== null && headers["content-length"] === undefined && headers["Content-Length"] === undefined) {
    headers["content-length"] = String(Buffer.byteLength(body));
  }
  return new Promise((resolve) => {
    let settled = false;
    const req = client.request({
      protocol: target.protocol,
      hostname: target.hostname,
      port: target.port,
      path: `${target.pathname}${target.search}`,
      method: requestSpec.method ?? "GET",
      headers,
      agent: options.agent,
      timeout: options.timeoutMs ?? 5000,
    }, (res) => {
      let bytes = 0;
      const chunks = [];
      res.on("data", (chunk) => {
        bytes += chunk.length;
        if (options.captureBody !== false) {
          chunks.push(chunk);
        }
      });
      res.on("end", () => {
        if (settled) {
          return;
        }
        settled = true;
        resolve({
          ok: true,
          timeout: false,
          statusCode: res.statusCode ?? 0,
          headers: res.headers,
          bytes,
          body: Buffer.concat(chunks).toString("utf8"),
          durationMs: performance.now() - started,
        });
      });
    });
    req.on("timeout", () => {
      if (settled) {
        return;
      }
      settled = true;
      req.destroy();
      resolve({
        ok: false,
        timeout: true,
        statusCode: 0,
        headers: {},
        bytes: 0,
        body: "",
        durationMs: performance.now() - started,
        error: "timeout",
      });
    });
    req.on("error", (error) => {
      if (settled) {
        return;
      }
      settled = true;
      resolve({
        ok: false,
        timeout: false,
        statusCode: 0,
        headers: {},
        bytes: 0,
        body: "",
        durationMs: performance.now() - started,
        error: error.message,
      });
    });
    if (body !== null) {
      req.write(body);
    }
    req.end();
  });
}

async function runPhase({ baseUrl, requests, connections, durationSeconds, timeoutMs, collect, seed }) {
  const samples = {
    totalRequests: 0,
    latencies: [],
    bytes: 0,
    errors: 0,
    timeouts: 0,
    non2xx: 0,
    errorTypes: {},
  };
  const keepAlive = new http.Agent({ keepAlive: true, maxSockets: connections, maxFreeSockets: connections });
  const pick = weightedPicker(requests, seed);
  const deadline = performance.now() + durationSeconds * 1000;
  async function worker(index) {
    while (performance.now() < deadline) {
      const spec = pick();
      const result = await singleRequest(baseUrl, spec, {
        agent: keepAlive,
        timeoutMs,
        captureBody: false,
      });
      if (!collect) {
        continue;
      }
      if (result.ok) {
        samples.totalRequests += 1;
        samples.latencies.push(result.durationMs);
        samples.bytes += result.bytes;
        if (result.statusCode < 200 || result.statusCode >= 300) {
          samples.non2xx += 1;
        }
      } else {
        samples.errors += 1;
        const key = result.error ?? (result.timeout ? "timeout" : "unknown");
        samples.errorTypes[key] = (samples.errorTypes[key] ?? 0) + 1;
        if (result.timeout) {
          samples.timeouts += 1;
        }
      }
    }
  }
  await Promise.all(Array.from({ length: connections }, (_, index) => worker(index)));
  keepAlive.destroy();
  return samples;
}

export async function runLoad(options) {
  const requests = options.requests.length > 0 ? options.requests : [options.request];
  if (options.warmupSeconds > 0) {
    await runPhase({
      baseUrl: options.baseUrl,
      requests,
      connections: options.connections,
      durationSeconds: options.warmupSeconds,
      timeoutMs: options.timeoutMs,
      collect: false,
      seed: options.seed ^ 0x9e3779b9,
    });
  }
  const samples = await runPhase({
    baseUrl: options.baseUrl,
    requests,
    connections: options.connections,
    durationSeconds: options.durationSeconds,
    timeoutMs: options.timeoutMs,
    collect: true,
    seed: options.seed,
  });
  return {
    ...summarizeLoad(samples, options.durationSeconds),
    raw: samples,
  };
}
