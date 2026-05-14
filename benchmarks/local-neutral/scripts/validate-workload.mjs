function stableJson(value) {
  if (Array.isArray(value)) return `[${value.map(stableJson).join(",")}]`;
  if (value && typeof value === "object") {
    return `{${Object.keys(value).sort().map((key) => `${JSON.stringify(key)}:${stableJson(value[key])}`).join(",")}}`;
  }
  return JSON.stringify(value);
}

function requestCases(workload) {
  if (!workload.mixed) {
    return [{ label: workload.name, request: workload, expected: workload }];
  }
  return workload.requests.map((request, index) => ({
    label: `${workload.name}[${index}:${request.method ?? "GET"} ${request.path}]`,
    request,
    expected: request,
  }));
}

async function readBody(response) {
  const text = await response.text();
  if (text === "") return { text, json: null, jsonError: null };
  try {
    return { text, json: JSON.parse(text), jsonError: null };
  } catch (error) {
    return { text, json: null, jsonError: error };
  }
}

function validateBody({ label, expected, responseBody }) {
  if (Object.hasOwn(expected, "expectedBody") && responseBody.text !== expected.expectedBody) {
    throw new Error(`${label} expected body ${JSON.stringify(expected.expectedBody)}, got ${JSON.stringify(responseBody.text)}`);
  }
  if (Object.hasOwn(expected, "expectedJson")) {
    if (responseBody.jsonError) {
      throw new Error(`${label} expected JSON response, got non-JSON body ${JSON.stringify(responseBody.text)}`);
    }
    const actual = stableJson(responseBody.json);
    const expectedJson = stableJson(expected.expectedJson);
    if (actual !== expectedJson) {
      throw new Error(`${label} expected JSON ${expectedJson}, got ${actual}`);
    }
  }
}

export async function validateWorkload({ workload, baseUrl, timeoutMs = 5000 }) {
  const checked = [];
  for (const item of requestCases(workload)) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeoutMs);
    const request = item.request;
    const expected = item.expected;
    const url = new URL(request.path ?? "/", baseUrl);
    try {
      const response = await fetch(url, {
        method: request.method ?? "GET",
        headers: request.headers ?? {},
        body: request.body ?? undefined,
        signal: controller.signal,
      });
      const expectedStatus = expected.expectedStatus ?? 200;
      if (response.status !== expectedStatus) {
        const body = await response.text();
        throw new Error(`${item.label} expected status ${expectedStatus}, got ${response.status}: ${body}`);
      }
      const responseBody = await readBody(response);
      validateBody({ label: item.label, expected, responseBody });
      checked.push({ label: item.label, status: response.status });
    } catch (error) {
      if (error?.name === "AbortError") {
        throw new Error(`${item.label} validation timed out after ${timeoutMs} ms`);
      }
      throw error;
    } finally {
      clearTimeout(timer);
    }
  }
  return { status: "PASS", checked };
}
