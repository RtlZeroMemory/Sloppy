import fs from "node:fs/promises";
import path from "node:path";

import { ContractAssertionCollector, errorInvariants } from "../runner/assertions.mjs";
import { exists, readJson, sha256File } from "../runner/artifact-utils.mjs";
import { createFinding, createReport } from "../runner/contract-report.mjs";
import { loadFixtures } from "../runner/fixture-loader.mjs";

const SUBSYSTEM = "http";
const ROUTE_ARTIFACT_HEADER_BYTES = 64;
const ROUTE_ARTIFACT_ENTRY_BYTES = 48;
const NO_BODY_STATUSES = new Set([204, 304]);
const NO_SELECTED_ROUTE_STATUSES = new Set([404, 405]);
const VOLATILE_HEADERS = new Set(["date", "server", "connection", "keep-alive", "transfer-encoding"]);
const EXECUTION_KINDS = new Set([
    "v8-handler",
    "native-static-text",
    "native-static-json",
    "native-static-empty",
    "native-static-problem",
]);
const NATIVE_EXECUTION_KINDS = new Set([
    "native-static-text",
    "native-static-json",
    "native-static-empty",
    "native-static-problem",
]);

const METHOD_CODES = new Map([
    [1, "GET"],
    [2, "POST"],
    [3, "PUT"],
    [4, "DELETE"],
    [5, "PATCH"],
    [6, "OPTIONS"],
    [7, "HEAD"],
]);
const EXECUTION_KIND_CODES = new Map([
    [1, "v8-handler"],
    [2, "native-static-text"],
    [3, "native-static-json"],
    [4, "native-static-empty"],
    [5, "native-static-problem"],
]);

function readU32(bytes, offset) {
    return bytes.readUInt32LE(offset);
}

function decodeSlice(bytes, offset, length) {
    return bytes.subarray(offset, offset + length).toString("utf8");
}

function parseLegacyRouteArtifact(text) {
    const lines = text.split(/\r?\n/u).filter((line) => line.length > 0);
    if (lines[0] !== "route-table-v1") {
        throw new Error("route artifact magic must be SLRT or route-table-v1");
    }
    return lines.slice(1).map((line, index) => {
        const [method, ...patternParts] = line.split(" ");
        return {
            method,
            pattern: patternParts.join(" "),
            handlerId: index + 1,
            executionKind: "v8-handler",
            name: "",
            paramCount: (patternParts.join(" ").match(/\{/gu) ?? []).length,
        };
    });
}

function parseBinaryRouteArtifact(bytes) {
    if (bytes.length < ROUTE_ARTIFACT_HEADER_BYTES || bytes.subarray(0, 4).toString("ascii") !== "SLRT") {
        return parseLegacyRouteArtifact(bytes.toString("utf8"));
    }
    const version = readU32(bytes, 4);
    if (version !== 1) {
        throw new Error(`unsupported routes.slrt version ${version}`);
    }
    const routeCount = readU32(bytes, 16);
    const tableOffset = readU32(bytes, 24);
    const stringOffset = readU32(bytes, 32);
    const stringSize = readU32(bytes, 36);
    const entries = [];
    const stringTable = bytes.subarray(stringOffset, stringOffset + stringSize);
    for (let index = 0; index < routeCount; index += 1) {
        const offset = tableOffset + index * ROUTE_ARTIFACT_ENTRY_BYTES;
        const method = METHOD_CODES.get(readU32(bytes, offset));
        const handlerId = readU32(bytes, offset + 4);
        const patternOffset = readU32(bytes, offset + 8);
        const patternLength = readU32(bytes, offset + 12);
        const nameOffset = readU32(bytes, offset + 16);
        const nameLength = readU32(bytes, offset + 20);
        const executionKind = EXECUTION_KIND_CODES.get(readU32(bytes, offset + 28));
        entries.push({
            method,
            handlerId,
            pattern: decodeSlice(stringTable, patternOffset, patternLength),
            name: decodeSlice(stringTable, nameOffset, nameLength),
            executionKind,
            paramCount: readU32(bytes, offset + 32),
        });
    }
    return entries;
}

function normalizeHeaders(headers = {}) {
    const normalized = new Map();
    for (const [key, value] of Object.entries(headers)) {
        normalized.set(key.toLowerCase(), String(value));
    }
    return normalized;
}

function contentTypeSemantics(value) {
    if (typeof value !== "string" || value.length === 0) {
        return "";
    }
    const [mediaType, ...parameters] = value.split(";");
    const sorted = parameters
        .map((part) => part.trim().toLowerCase())
        .filter((part) => part.length > 0)
        .sort();
    return [mediaType.trim().toLowerCase(), ...sorted].join(";");
}

function responseBodyBytes(response) {
    if (Array.isArray(response.bodyBytes)) {
        return Buffer.from(response.bodyBytes);
    }
    return Buffer.from(String(response.body ?? ""), "utf8");
}

function responseContentType(response) {
    return response.contentType ?? normalizeHeaders(response.headers).get("content-type") ?? "";
}

function contentLengthValue(response) {
    return normalizeHeaders(response.headers).get("content-length");
}

function importantHeaders(response) {
    const result = {};
    for (const [key, value] of normalizeHeaders(response.headers)) {
        if (!VOLATILE_HEADERS.has(key) && key !== "content-length") {
            result[key] = value;
        }
    }
    return result;
}

function jsonBody(response) {
    try {
        return JSON.parse(responseBodyBytes(response).toString("utf8"));
    } catch {
        return undefined;
    }
}

function isProblemResponse(response) {
    return contentTypeSemantics(responseContentType(response)).startsWith("application/problem+json");
}

function routeKey(route) {
    return `${route.method ?? ""} ${route.pattern ?? ""}`;
}

function byRouteKey(routes) {
    return new Map(routes.map((route) => [routeKey(route), route]));
}

async function filesEqual(leftPath, rightPath) {
    const [left, right] = await Promise.all([fs.readFile(leftPath), fs.readFile(rightPath)]);
    return left.equals(right);
}

async function assertCompilerGeneratedProvenance(collector, root, config, resolvedPlanPath, resolvedArtifactPath) {
    const provenance = config.compilerGenerated;
    if (provenance === undefined) {
        return;
    }
    const expectedPlanPath = path.resolve(root, provenance.planPath ?? "");
    if (await filesEqual(resolvedPlanPath, expectedPlanPath)) {
        collector.pass("fixture.compiler-generated-plan", "fixture Plan matches compiler-generated Plan golden", {
            path: provenance.planPath,
        });
    } else {
        collector.fail("fixture.compiler-generated-plan", "fixture Plan must match compiler-generated Plan golden", {
            path: provenance.planPath,
        });
    }
    if (provenance.routeArtifactPath === undefined || resolvedArtifactPath === undefined) {
        collector.fail("fixture.compiler-generated-route-artifact", "compiler-generated fixture must compare routes.slrt provenance", {
            path: provenance.routeArtifactPath,
        });
        return;
    }
    const expectedArtifactPath = path.resolve(root, provenance.routeArtifactPath);
    if (await filesEqual(resolvedArtifactPath, expectedArtifactPath)) {
        collector.pass("fixture.compiler-generated-route-artifact", "fixture routes.slrt matches compiler-generated route artifact golden", {
            path: provenance.routeArtifactPath,
        });
    } else {
        collector.fail("fixture.compiler-generated-route-artifact", "fixture routes.slrt must match compiler-generated route artifact golden", {
            path: provenance.routeArtifactPath,
        });
    }
}

function routeSpecificity(pattern) {
    const segments = pattern.split("/").filter(Boolean);
    let staticSegments = 0;
    let paramSegments = 0;
    let constrainedParamSegments = 0;
    for (const segment of segments) {
        if (/^\{[^}]+\}$/u.test(segment)) {
            paramSegments += 1;
            if (/^\{[^:}]+:[^}]+\}$/u.test(segment)) {
                constrainedParamSegments += 1;
            }
        } else {
            staticSegments += 1;
        }
    }
    return { staticSegments, paramSegments, constrainedParamSegments, segments: segments.length };
}

function parsePatternParam(segment) {
    const inner = segment.slice(1, -1);
    const [name, constraint = "str"] = inner.split(":");
    return { name, constraint };
}

function decodeUrlPathSegment(segment) {
    try {
        return decodeURIComponent(segment);
    } catch {
        return segment;
    }
}

function routeMatches(route, request) {
    if (route.method !== request.method) {
        return undefined;
    }
    const patternSegments = route.pattern.split("/").filter(Boolean);
    const pathOnly = String(request.path ?? "").split("?", 1)[0];
    const requestSegments = pathOnly.split("/").filter(Boolean);
    if (patternSegments.length !== requestSegments.length) {
        return undefined;
    }
    const params = {};
    for (const [index, patternSegment] of patternSegments.entries()) {
        const requestSegment = decodeUrlPathSegment(requestSegments[index]);
        if (/^\{[^}]+\}$/u.test(patternSegment)) {
            const param = parsePatternParam(patternSegment);
            if (param.constraint === "int" && !/^-?\d+$/u.test(requestSegment)) {
                return undefined;
            }
            params[param.name] = requestSegment;
            continue;
        }
        if (patternSegment !== requestSegment) {
            return undefined;
        }
    }
    return params;
}

function selectRoute(routes, request) {
    const candidates = [];
    for (const [index, route] of routes.entries()) {
        const params = routeMatches(route, request);
        if (params !== undefined) {
            candidates.push({ index, route, params, specificity: routeSpecificity(route.pattern) });
        }
    }
    candidates.sort((left, right) => {
        if (right.specificity.staticSegments !== left.specificity.staticSegments) {
            return right.specificity.staticSegments - left.specificity.staticSegments;
        }
        if (left.specificity.paramSegments !== right.specificity.paramSegments) {
            return left.specificity.paramSegments - right.specificity.paramSegments;
        }
        if (right.specificity.constrainedParamSegments !== left.specificity.constrainedParamSegments) {
            return right.specificity.constrainedParamSegments - left.specificity.constrainedParamSegments;
        }
        return left.index - right.index;
    });
    return candidates[0];
}

function collectAllowedMethods(routes, request) {
    const probe = { ...request, method: "" };
    const methods = new Set();
    for (const route of routes) {
        probe.method = route.method;
        if (routeMatches(route, probe) !== undefined) {
            methods.add(route.method);
        }
    }
    return [...methods].sort();
}

function canonicalJson(value) {
    if (Array.isArray(value)) {
        return value.map(canonicalJson);
    }
    if (value !== null && typeof value === "object") {
        return Object.fromEntries(Object.keys(value).sort().map((key) => [key, canonicalJson(value[key])]));
    }
    return value;
}

function sameJsonShape(left, right) {
    return JSON.stringify(canonicalJson(left)) === JSON.stringify(canonicalJson(right));
}

function assertResponseEquivalence(collector, trace) {
    const generic = trace.generic;
    const optimized = trace.optimized;
    if (generic.status === optimized.status) {
        collector.pass("http.status-equivalence", "optimized response status matches generic response", {
            request: trace.name,
            status: generic.status,
        });
    } else {
        collector.fail("http.status-equivalence", "optimized response status must match generic response", {
            request: trace.name,
            expected: generic.status,
            actual: optimized.status,
        });
    }

    const genericBody = responseBodyBytes(generic);
    const optimizedBody = responseBodyBytes(optimized);
    if (genericBody.equals(optimizedBody)) {
        collector.pass("http.body-equivalence", "optimized response body bytes match generic response", {
            request: trace.name,
        });
    } else {
        collector.fail("http.body-equivalence", "optimized response body bytes must match generic response", {
            request: trace.name,
            expected: genericBody.toString("utf8"),
            actual: optimizedBody.toString("utf8"),
        });
    }

    const genericContentType = contentTypeSemantics(responseContentType(generic));
    const optimizedContentType = contentTypeSemantics(responseContentType(optimized));
    if (genericContentType === optimizedContentType) {
        collector.pass("http.content-type-equivalence", "optimized content type matches generic semantics", {
            request: trace.name,
            contentType: genericContentType,
        });
    } else {
        collector.fail("http.content-type-equivalence", "optimized content type semantics must match generic response", {
            request: trace.name,
            expected: genericContentType,
            actual: optimizedContentType,
        });
    }

    for (const [label, response] of [["generic", generic], ["optimized", optimized]]) {
        const contentLength = contentLengthValue(response);
        if (contentLength === undefined) {
            continue;
        }
        if (trace.request?.method === "HEAD") {
            collector.pass("http.content-length-correct", "HEAD response content-length is not body-byte validated", {
                request: trace.name,
                lane: label,
            });
            continue;
        }
        const actual = Number(contentLength);
        const expected = responseBodyBytes(response).length;
        if (Number.isInteger(actual) && actual === expected) {
            collector.pass("http.content-length-correct", "content-length matches response body bytes", {
                request: trace.name,
                lane: label,
            });
        } else {
            collector.fail("http.content-length-correct", "content-length must match response body bytes", {
                request: trace.name,
                lane: label,
                expected,
                actual: contentLength,
            });
        }
    }

    if (!NO_BODY_STATUSES.has(generic.status) && !NO_BODY_STATUSES.has(optimized.status)) {
        collector.pass("http.no-body-status", "response status allows a body", { request: trace.name });
    } else if (genericBody.length === 0 && optimizedBody.length === 0) {
        collector.pass("http.no-body-status", "204/304 response has no body", { request: trace.name });
    } else {
        collector.fail("http.no-body-status", "204/304 response must not include a body", {
            request: trace.name,
            genericBytes: genericBody.length,
            optimizedBytes: optimizedBody.length,
        });
    }

    if (sameJsonShape(importantHeaders(generic), importantHeaders(optimized))) {
        collector.pass("http.headers-equivalence", "important headers match after volatile headers are excluded", {
            request: trace.name,
        });
    } else {
        collector.fail("http.headers-equivalence", "important headers must match after volatile headers are excluded", {
            request: trace.name,
            expected: importantHeaders(generic),
            actual: importantHeaders(optimized),
        });
    }

    if (isProblemResponse(generic) || isProblemResponse(optimized)) {
        const genericProblem = jsonBody(generic);
        const optimizedProblem = jsonBody(optimized);
        const genericShape = {
            status: genericProblem?.status,
            title: genericProblem?.title,
            type: genericProblem?.type,
        };
        const optimizedShape = {
            status: optimizedProblem?.status,
            title: optimizedProblem?.title,
            type: optimizedProblem?.type,
        };
        if (sameJsonShape(genericShape, optimizedShape)) {
            collector.pass("http.problem-details-shape", "ProblemDetails status/title/type shape matches", {
                request: trace.name,
            });
        } else {
            collector.fail("http.problem-details-shape", "ProblemDetails status/title/type shape must match", {
                request: trace.name,
                expected: genericShape,
                actual: optimizedShape,
            });
        }
    }
}

function assertTraceRouteEquivalence(collector, trace) {
    for (const field of ["routePattern", "routeName", "method", "handlerId"]) {
        if (trace.generic.route?.[field] === trace.optimized.route?.[field]) {
            collector.pass(`http.${field}-equivalence`, `${field} matches between generic and optimized dispatch`, {
                request: trace.name,
                value: trace.generic.route?.[field],
            });
        } else {
            collector.fail(`http.${field}-equivalence`, `${field} must match between generic and optimized dispatch`, {
                request: trace.name,
                expected: trace.generic.route?.[field],
                actual: trace.optimized.route?.[field],
            });
        }
    }
    if (sameJsonShape(trace.generic.route?.params ?? {}, trace.optimized.route?.params ?? {})) {
        collector.pass("dispatch.param-equivalence", "route params match between generic and optimized dispatch", {
            request: trace.name,
        });
    } else {
        collector.fail("dispatch.param-equivalence", "route params must match between generic and optimized dispatch", {
            request: trace.name,
            expected: trace.generic.route?.params,
            actual: trace.optimized.route?.params,
        });
    }
    if (sameJsonShape(trace.generic.query ?? {}, trace.optimized.query ?? {})) {
        collector.pass("http.query-equivalence", "query params match between generic and optimized dispatch", {
            request: trace.name,
        });
    } else {
        collector.fail("http.query-equivalence", "query params must match between generic and optimized dispatch", {
            request: trace.name,
            expected: trace.generic.query,
            actual: trace.optimized.query,
        });
    }
}

function assertNativeResponse(collector, route, trace) {
    if (!NATIVE_EXECUTION_KINDS.has(route.dispatch?.executionKind)) {
        return;
    }
    if (trace.generic.status === route.nativeResponse?.status) {
        collector.pass("native-response.status-equivalence", "native no-JS response status matches generic handler", {
            request: trace.name,
            route: routeKey(route),
        });
    } else {
        collector.fail("native-response.status-equivalence", "native no-JS response status must match generic handler", {
            request: trace.name,
            route: routeKey(route),
            expected: trace.generic.status,
            actual: route.nativeResponse?.status,
        });
    }
    const nativeBody = Buffer.from(String(route.nativeResponse?.body ?? ""), "utf8");
    const genericBody = responseBodyBytes(trace.generic);
    if (nativeBody.equals(genericBody)) {
        collector.pass("native-response.body-equivalence", "native no-JS response body matches generic handler", {
            request: trace.name,
            route: routeKey(route),
        });
    } else {
        collector.fail("native-response.body-equivalence", "native no-JS response body must match generic handler", {
            request: trace.name,
            route: routeKey(route),
            expected: genericBody.toString("utf8"),
            actual: nativeBody.toString("utf8"),
        });
    }
    const nativeContentType = contentTypeSemantics(route.nativeResponse?.contentType ?? "");
    const genericContentType = contentTypeSemantics(responseContentType(trace.generic));
    if (nativeContentType === genericContentType) {
        collector.pass("native-response.content-type-equivalence", "native no-JS content type matches generic handler", {
            request: trace.name,
            route: routeKey(route),
        });
    } else {
        collector.fail("native-response.content-type-equivalence", "native no-JS content type must match generic handler", {
            request: trace.name,
            route: routeKey(route),
            expected: genericContentType,
            actual: nativeContentType,
        });
    }
    if (route.nativeResponse?.requiresRequestContext === true) {
        collector.fail("native-response.context-materialization", "native no-JS response must not require request context unless metadata says so", {
            request: trace.name,
            route: routeKey(route),
        });
    } else {
        collector.pass("native-response.context-materialization", "native no-JS response does not require request context", {
            request: trace.name,
            route: routeKey(route),
        });
    }
}

async function validateHttpDispatchFixture({ root, fixture }) {
    const collector = new ContractAssertionCollector({ subsystem: SUBSYSTEM, fixture });
    const config = await readFixtureConfig(root);
    const baseRoot =
        typeof config.baseFixture === "string" && config.baseFixture.length > 0
            ? path.resolve(root, "..", config.baseFixture)
            : root;
    const planPath = path.join(root, "app.plan.json");
    const tracePath = path.join(root, "http-traces.json");
    const resolvedPlanPath = (await exists(planPath)) ? planPath : path.join(baseRoot, "app.plan.json");
    const resolvedTracePath = (await exists(tracePath)) ? tracePath : path.join(baseRoot, "http-traces.json");
    if (!(await exists(resolvedPlanPath))) {
        collector.fail("plan.present", "HTTP contract fixture must include app.plan.json");
        return collector.findings;
    }
    if (!(await exists(resolvedTracePath))) {
        collector.fail("http.traces-present", "HTTP contract fixture must include http-traces.json");
        return collector.findings;
    }
    const plan = applyJsonReplacements(await readJson(resolvedPlanPath), config.planReplacements ?? []);
    const traces = applyJsonReplacements(await readJson(resolvedTracePath), config.traceReplacements ?? []);
    const routes = Array.isArray(plan.routes) ? plan.routes : [];
    const routeMap = byRouteKey(routes);
    const routeDispatch = plan.routeDispatch ?? {};
    let artifactEntries = [];
    let resolvedArtifactPath;
    if (routeDispatch.artifact?.path !== undefined) {
        const artifactPath = path.join(root, routeDispatch.artifact.path);
        const baseArtifactPath = path.join(baseRoot, routeDispatch.artifact.path);
        resolvedArtifactPath = (await exists(artifactPath)) ? artifactPath : baseArtifactPath;
        if (config.removeRouteArtifact === true || !(await exists(resolvedArtifactPath))) {
            collector.fail("dispatch.route-artifact-present", "Plan routeDispatch artifact must exist", {
                path: routeDispatch.artifact.path,
            });
        } else {
            collector.pass("dispatch.route-artifact-present", "Plan routeDispatch artifact exists", {
                path: routeDispatch.artifact.path,
            });
            const bytes = await fs.readFile(resolvedArtifactPath);
            const actualHash = await sha256File(resolvedArtifactPath);
            if (routeDispatch.artifact.hash === actualHash) {
                collector.pass("dispatch.route-artifact-hash", "routes.slrt hash matches Plan metadata");
            } else {
                collector.fail("dispatch.route-artifact-hash", "routes.slrt hash must match Plan metadata", {
                    expected: routeDispatch.artifact.hash,
                    actual: actualHash,
                });
            }
            try {
                artifactEntries = parseBinaryRouteArtifact(bytes);
                collector.pass("dispatch.route-artifact-parse", "routes.slrt parses");
            } catch (error) {
                collector.fail("dispatch.route-artifact-parse", "routes.slrt must parse", { error: error.message });
            }
        }
    } else {
        collector.fail("dispatch.route-artifact-present", "HTTP dispatch contracts require Plan routeDispatch artifact metadata");
    }

    await assertCompilerGeneratedProvenance(collector, root, config, resolvedPlanPath, resolvedArtifactPath);

    if (artifactEntries.length === routes.length) {
        collector.pass("dispatch.route-count", "native dispatch route count matches Plan route count", {
            count: routes.length,
        });
    } else {
        collector.fail("dispatch.route-count", "native dispatch route count must match Plan route count", {
            expected: routes.length,
            actual: artifactEntries.length,
        });
    }

    const nativeNoJsRoutes = routes.filter((route) => NATIVE_EXECUTION_KINDS.has(route.dispatch?.executionKind));
    if (routeDispatch.nativeNoJsEndpoints === nativeNoJsRoutes.length) {
        collector.pass("native-response.endpoint-count", "native no-JS endpoint count matches Plan routes", {
            count: nativeNoJsRoutes.length,
        });
    } else {
        collector.fail("native-response.endpoint-count", "native no-JS endpoint count must match Plan routes", {
            expected: nativeNoJsRoutes.length,
            actual: routeDispatch.nativeNoJsEndpoints,
        });
    }

    for (const [index, route] of routes.entries()) {
        const key = routeKey(route);
        const executionKind = route.dispatch?.executionKind;
        if (EXECUTION_KINDS.has(executionKind)) {
            collector.pass("dispatch.execution-kind-known", "route dispatch execution kind is supported", {
                route: key,
                executionKind,
            });
        } else {
            collector.fail("dispatch.execution-kind-known", "route dispatch execution kind must be supported", {
                route: key,
                executionKind,
            });
        }
        if (route.nativeDispatchSupported === false && executionKind !== "v8-handler") {
            collector.fail("dispatch.execution-kind-honesty", "unsupported native route shape must fall back to V8", {
                route: key,
                executionKind,
            });
        } else if (route.nativeDispatchSupported === false) {
            collector.pass("dispatch.execution-kind-honesty", "unsupported native route shape falls back to V8", {
                route: key,
            });
        }
        if ((route.effects ?? []).length > 0 && executionKind !== "v8-handler") {
            collector.fail("native-response.provider-effects-v8", "provider-effect routes must execute through V8", {
                route: key,
                executionKind,
            });
        } else if ((route.effects ?? []).length > 0) {
            collector.pass("native-response.provider-effects-v8", "provider-effect route executes through V8", {
                route: key,
            });
        }
        if ((route.effects ?? []).length > 0 && route.nativeResponse !== undefined) {
            collector.fail("native-response.provider-effects-no-native-response", "provider-effect routes must not be marked native no-JS", {
                route: key,
            });
        } else if ((route.effects ?? []).length > 0) {
            collector.pass("native-response.provider-effects-no-native-response", "provider-effect route has no native no-JS response", {
                route: key,
            });
        }
        const artifact = artifactEntries[index];
        if (artifact === undefined) {
            continue;
        }
        if (artifact.pattern === route.pattern) {
            collector.pass("dispatch.route-pattern", "routes.slrt pattern matches Plan route", { route: key });
        } else {
            collector.fail("dispatch.route-pattern", "routes.slrt pattern must match Plan route", {
                route: key,
                expected: route.pattern,
                actual: artifact.pattern,
            });
        }
        if (artifact.method === route.method) {
            collector.pass("dispatch.route-method", "routes.slrt method matches Plan route", { route: key });
        } else {
            collector.fail("dispatch.route-method", "routes.slrt method must match Plan route", {
                route: key,
                expected: route.method,
                actual: artifact.method,
            });
        }
        if (artifact.executionKind === executionKind) {
            collector.pass("dispatch.execution-kind-honesty", "routes.slrt execution kind matches Plan route", {
                route: key,
                executionKind,
            });
        } else {
            collector.fail("dispatch.execution-kind-honesty", "routes.slrt execution kind must match Plan route", {
                route: key,
                expected: executionKind,
                actual: artifact.executionKind,
            });
        }
    }

    for (const trace of traces.requests ?? []) {
        assertResponseEquivalence(collector, trace);
        assertTraceRouteEquivalence(collector, trace);

        const selectedPlan = selectRoute(routes, trace.request);
        if (selectedPlan !== undefined && selectedPlan.route.pattern === trace.generic.route?.routePattern) {
            collector.pass("dispatch.route-priority", "Plan route priority selects the same route as generic dispatch", {
                request: trace.name,
                route: selectedPlan.route.pattern,
            });
        } else if (selectedPlan === undefined && NO_SELECTED_ROUTE_STATUSES.has(trace.generic.status)) {
            collector.pass("dispatch.route-priority", "Plan route priority agrees with generic unmatched dispatch", {
                request: trace.name,
            });
        } else {
            collector.fail("dispatch.route-priority", "Plan route priority must select the generic dispatch route", {
                request: trace.name,
                expected: trace.generic.route?.routePattern,
                actual: selectedPlan?.route.pattern,
            });
        }

        const selectedArtifact = selectRoute(artifactEntries, trace.request);
        if (selectedArtifact !== undefined && selectedArtifact.route.pattern === trace.generic.route?.routePattern) {
            collector.pass("dispatch.route-priority", "routes.slrt route priority selects the same route as generic dispatch", {
                request: trace.name,
                route: selectedArtifact.route.pattern,
            });
        } else if (selectedArtifact === undefined && NO_SELECTED_ROUTE_STATUSES.has(trace.generic.status)) {
            collector.pass("dispatch.route-priority", "routes.slrt route priority agrees with generic unmatched dispatch", {
                request: trace.name,
            });
        } else {
            collector.fail("dispatch.route-priority", "routes.slrt route priority must select the generic dispatch route", {
                request: trace.name,
                expected: trace.generic.route?.routePattern,
                actual: selectedArtifact?.route.pattern,
            });
        }

        if (selectedPlan !== undefined && sameJsonShape(selectedPlan.params, trace.generic.route?.params ?? {})) {
            collector.pass("dispatch.param-equivalence", "Plan route extraction matches generic route params", {
                request: trace.name,
            });
        } else if (selectedPlan === undefined && NO_SELECTED_ROUTE_STATUSES.has(trace.generic.status)) {
            collector.pass("dispatch.param-equivalence", "no route params are extracted for generic unmatched dispatch", {
                request: trace.name,
            });
        } else {
            collector.fail("dispatch.param-equivalence", "Plan route extraction must match generic route params", {
                request: trace.name,
                expected: trace.generic.route?.params,
                actual: selectedPlan?.params,
            });
        }

        const allowedMethods = collectAllowedMethods(routes, trace.request);
        if (selectedPlan !== undefined) {
            collector.pass("http.method-selection", "method selection is consistent with route table", {
                request: trace.name,
                allowedMethods,
            });
        } else if (allowedMethods.length > 0 && trace.generic.status === 405) {
            collector.pass("http.method-selection", "method selection is consistent with route table", {
                request: trace.name,
                allowedMethods,
            });
        } else if (allowedMethods.length > 0) {
            collector.fail("http.method-selection", "request with alternate methods must produce a 405 response", {
                request: trace.name,
                expected: 405,
                actual: trace.generic.status,
                allowedMethods,
            });
        } else if (trace.generic.status === 405) {
            collector.fail("http.method-selection", "405 response must identify a path with alternate methods", {
                request: trace.name,
                allowedMethods,
            });
        } else {
            collector.pass("http.method-selection", "method selection is consistent with route table", {
                request: trace.name,
                allowedMethods,
            });
        }

        const selectedRoute = selectedPlan?.route !== undefined ? routeMap.get(routeKey(selectedPlan.route)) : undefined;
        if (selectedRoute !== undefined) {
            assertNativeResponse(collector, selectedRoute, trace);
        }
    }

    collector.warn(
        "native-response.v8-call-counter-unavailable",
        "runtime counters are not available in this fixture lane; semantic equivalence is checked now, and zero V8 calls need runtime counters or profiling support",
    );

    return collector.findings;
}

async function readFixtureConfig(root) {
    const configPath = path.join(root, "contract-fixture.json");
    if (!(await exists(configPath))) {
        return {};
    }
    return readJson(configPath);
}

function cloneJson(value) {
    return JSON.parse(JSON.stringify(value));
}

function applyJsonReplacements(value, replacements) {
    const copy = cloneJson(value);
    for (const replacement of replacements) {
        const pathSegments = replacement.path;
        if (!Array.isArray(pathSegments) || pathSegments.length === 0) {
            continue;
        }
        let target = copy;
        for (const segment of pathSegments.slice(0, -1)) {
            target = target?.[segment];
        }
        if (target !== undefined && target !== null) {
            target[pathSegments.at(-1)] = replacement.value;
        }
    }
    return copy;
}

function detectedErrorDetails(rawFindings) {
    return rawFindings
        .filter((finding) => finding.status === "fail" && finding.severity === "error")
        .map((finding) => ({
            invariant: finding.invariant,
            message: finding.message,
            path: finding.path,
            details: finding.details,
        }));
}

function expectedFailureFinding(fixture, invariant, detected, rawFindings) {
    return createFinding({
        id: `${SUBSYSTEM}.${fixture}.negative.${invariant}`,
        status: "pass",
        severity: "info",
        subsystem: SUBSYSTEM,
        invariant: `negative.${invariant}`,
        fixture,
        message: `broken fixture produced expected ${invariant} finding`,
        details: {
            detected,
            detectedFindings: detectedErrorDetails(rawFindings),
        },
    });
}

function failedExpectationFinding(fixture, invariant, detected, rawFindings) {
    return createFinding({
        id: `${SUBSYSTEM}.${fixture}.negative.${invariant}.missing`,
        status: "fail",
        severity: "error",
        subsystem: SUBSYSTEM,
        invariant: `negative.${invariant}`,
        fixture,
        message: `broken fixture did not produce expected ${invariant} finding`,
        details: {
            detected,
            detectedFindings: detectedErrorDetails(rawFindings),
        },
    });
}

function unexpectedErrorFinding(fixture, invariant, expectedInvariants, rawFindings) {
    return createFinding({
        id: `${SUBSYSTEM}.${fixture}.negative.${invariant}.unexpected`,
        status: "pass",
        severity: "warning",
        subsystem: SUBSYSTEM,
        invariant: `negative.unexpected.${invariant}`,
        fixture,
        message: `broken fixture produced unexpected ${invariant} finding`,
        details: {
            expectedInvariants,
            detectedFindings: detectedErrorDetails(rawFindings),
        },
    });
}

export async function runHttpDispatchContract({ repoRoot, tier }) {
    const startedAt = new Date().toISOString();
    const fixtures = await loadFixtures(path.join(repoRoot, "tests/contracts/http/fixtures"));
    const findings = [];
    for (const fixture of fixtures) {
        const rawFindings = await validateHttpDispatchFixture({ root: fixture.root, fixture: fixture.name });
        const detected = [...new Set(errorInvariants(rawFindings))];
        if (fixture.expected === "fail") {
            const expectedInvariants = new Set(fixture.expectedInvariants);
            for (const invariant of fixture.expectedInvariants) {
                findings.push(
                    detected.includes(invariant)
                        ? expectedFailureFinding(fixture.name, invariant, detected, rawFindings)
                        : failedExpectationFinding(fixture.name, invariant, detected, rawFindings),
                );
            }
            for (const invariant of detected) {
                if (!expectedInvariants.has(invariant)) {
                    findings.push(
                        unexpectedErrorFinding(fixture.name, invariant, fixture.expectedInvariants, rawFindings),
                    );
                }
            }
            continue;
        }
        findings.push(...rawFindings);
    }

    return createReport({
        subsystem: SUBSYSTEM,
        tier,
        startedAt,
        finishedAt: new Date().toISOString(),
        findings,
    });
}
