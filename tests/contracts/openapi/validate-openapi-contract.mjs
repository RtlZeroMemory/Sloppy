import fs from "node:fs/promises";
import path from "node:path";

import { ContractAssertionCollector, errorInvariants } from "../runner/assertions.mjs";
import { readJson } from "../runner/artifact-utils.mjs";
import { createFinding, createReport } from "../runner/contract-report.mjs";
import { loadFixtures } from "../runner/fixture-loader.mjs";

const SUBSYSTEM = "openapi";
const HTTP_METHODS = new Set(["get", "put", "post", "delete", "options", "head", "patch", "trace"]);
const PROBLEM_CONTENT_TYPE = "application/problem+json";

function isObject(value) {
    return value !== null && typeof value === "object" && !Array.isArray(value);
}

async function readFixtureConfig(fixture) {
    const configPath = path.join(fixture.root, "contract-fixture.json");
    return JSON.parse(await fs.readFile(configPath, "utf8"));
}

function resolveFixturePath(repoRoot, fixtureRoot, configuredPath) {
    if (typeof configuredPath !== "string" || configuredPath.length === 0) {
        throw new Error("OpenAPI contract fixture must configure plan and openapi paths.");
    }
    return path.resolve(fixtureRoot, configuredPath).startsWith(repoRoot)
        ? path.resolve(fixtureRoot, configuredPath)
        : path.resolve(repoRoot, configuredPath);
}

function cloneJson(value) {
    return JSON.parse(JSON.stringify(value));
}

function getAt(root, keys) {
    let cursor = root;
    for (const key of keys) {
        if (cursor === undefined || cursor === null) {
            return undefined;
        }
        cursor = cursor[key];
    }
    return cursor;
}

function parentAt(root, keys) {
    if (!Array.isArray(keys) || keys.length === 0) {
        throw new Error("mutation path must be a non-empty array");
    }
    return {
        parent: getAt(root, keys.slice(0, -1)),
        key: keys.at(-1),
    };
}

function applyMutation(document, mutation) {
    const { parent, key } = parentAt(document, mutation.path);
    if (parent === undefined || parent === null) {
        throw new Error(`mutation parent not found: ${mutation.path.join(".")}`);
    }
    if (mutation.op === "delete") {
        if (Array.isArray(parent)) {
            parent.splice(Number(key), 1);
        } else {
            delete parent[key];
        }
        return;
    }
    if (mutation.op === "set") {
        parent[key] = cloneJson(mutation.value);
        return;
    }
    if (mutation.op === "append") {
        const target = getAt(document, mutation.path);
        if (!Array.isArray(target)) {
            throw new Error(`append mutation target is not an array: ${mutation.path.join(".")}`);
        }
        target.push(cloneJson(mutation.value));
        return;
    }
    if (mutation.op === "renameKey") {
        const target = getAt(document, mutation.path);
        if (!isObject(target) || !Object.hasOwn(target, mutation.from)) {
            throw new Error(`renameKey mutation source not found: ${mutation.path.join(".")}.${mutation.from}`);
        }
        target[mutation.to] = target[mutation.from];
        delete target[mutation.from];
        return;
    }
    throw new Error(`unsupported OpenAPI fixture mutation: ${mutation.op}`);
}

function applyMutations(document, mutations = []) {
    const mutated = cloneJson(document);
    for (const mutation of mutations) {
        applyMutation(mutated, mutation);
    }
    return mutated;
}

function publicRoutes(plan) {
    return (Array.isArray(plan?.routes) ? plan.routes : []).filter((route) => {
        if (!isObject(route) || typeof route.method !== "string" || typeof route.pattern !== "string") {
            return false;
        }
        if (route.docsInternal === true || route.docs_internal === true) {
            return false;
        }
        if (route.dynamicMetadata === true || route.dynamic_metadata === true) {
            return false;
        }
        return true;
    });
}

function routeLabel(route) {
    return `${route.method.toUpperCase()} ${route.pattern}`;
}

function openApiPathForPattern(pattern) {
    return pattern.replace(/\{([^}:]+):[^}]+\}/gu, "{$1}");
}

function routeParamNames(pattern) {
    const names = [];
    for (const match of pattern.matchAll(/\{([^}:]+)(?::[^}]+)?\}/gu)) {
        names.push(match[1]);
    }
    return names;
}

function hasTypedRouteParameter(pattern) {
    return /\{[^}:]+:[^}]+\}/u.test(pattern);
}

function operationFor(openapi, route) {
    const pathItem = openapi?.paths?.[openApiPathForPattern(route.pattern)];
    if (!isObject(pathItem)) {
        return { pathItem: undefined, operation: undefined };
    }
    return { pathItem, operation: pathItem[route.method.toLowerCase()] };
}

function parametersByLocation(operation, location) {
    return (Array.isArray(operation?.parameters) ? operation.parameters : []).filter(
        (parameter) => isObject(parameter) && parameter.in === location && typeof parameter.name === "string",
    );
}

function schemaNames(plan) {
    return new Set((Array.isArray(plan?.schemas) ? plan.schemas : []).map((schema) => schema?.name).filter(Boolean));
}

function schemaByName(plan, name) {
    return (Array.isArray(plan?.schemas) ? plan.schemas : []).find((schema) => schema?.name === name);
}

function schemaPropertyNames(plan, schemaName) {
    const schema = schemaByName(plan, schemaName);
    const properties = schema?.definition?.properties;
    return isObject(properties) ? Object.keys(properties) : [];
}

function bindings(route, kind) {
    return (Array.isArray(route.bindings) ? route.bindings : []).filter((binding) => binding?.kind === kind);
}

function expectedQueryParameters(plan, route) {
    const names = new Set();
    if (typeof route.querySchema === "string") {
        for (const name of schemaPropertyNames(plan, route.querySchema)) {
            names.add(name);
        }
    }
    for (const binding of bindings(route, "query")) {
        if (typeof binding.name === "string" && binding.name.length > 0) {
            names.add(binding.name);
        }
    }
    return [...names].sort();
}

function expectedHeaderParameters(route) {
    const names = new Set();
    const headers = Array.isArray(route.headers) ? route.headers : [];
    for (const header of headers) {
        if (typeof header?.name === "string" && header.name.length > 0) {
            names.add(header.name);
        }
    }
    for (const binding of bindings(route, "header")) {
        if (typeof binding.name === "string" && binding.name.length > 0) {
            names.add(binding.name);
        }
    }
    return [...names].sort();
}

function bodyBindings(route) {
    return bindings(route, "body.json")
        .concat(bindings(route, "body.form"))
        .concat(bindings(route, "body.multipart"));
}

function routeHasKnownBodySchema(plan, route) {
    const knownSchemas = schemaNames(plan);
    return bodyBindings(route).some(
        (binding) =>
            binding.kind === "body.form" ||
            binding.kind === "body.multipart" ||
            (typeof binding.schema === "string" && knownSchemas.has(binding.schema)),
    );
}

function routeHasBodyBinding(route) {
    return bodyBindings(route).length > 0;
}

function staticResponseStatuses(route) {
    const responseEntries = Array.isArray(route.responses) ? route.responses : [];
    if (responseEntries.length > 0) {
        return responseEntries.map((response) => String(response.status ?? 200));
    }
    if (isObject(route.response) && (typeof route.response.kind === "string" || route.response.status !== undefined)) {
        return [String(route.response.status === 0 || route.response.status === undefined ? 200 : route.response.status)];
    }
    return [];
}

function responseNeedsSchema(plan, route) {
    const knownSchemas = schemaNames(plan);
    const responses = Array.isArray(route.responses) && route.responses.length > 0 ? route.responses : [route.response];
    return responses.some(
        (response) =>
            isObject(response) &&
            response.kind === "json" &&
            (typeof response.bodySchema !== "string" || !knownSchemas.has(response.bodySchema)),
    );
}

function responseMetadataUnknown(route) {
    return !isObject(route.response) && (!Array.isArray(route.responses) || route.responses.length === 0);
}

function routeIsPlanComplete(plan, route) {
    const completeness = isObject(route.completeness) ? route.completeness.status : route.completeness;
    return (
        completeness === "complete" &&
        (!routeHasBodyBinding(route) || routeHasKnownBodySchema(plan, route)) &&
        !responseNeedsSchema(plan, route) &&
        !responseMetadataUnknown(route) &&
        (!route.auth?.required || Array.isArray(plan?.auth?.schemes))
    );
}

function hasPartialMarker(value) {
    if (value === undefined) {
        return false;
    }
    if (isObject(value) && typeof value["x-slop-partial"] === "string") {
        return true;
    }
    if (Array.isArray(value)) {
        return value.some((entry) => hasPartialMarker(entry));
    }
    if (isObject(value)) {
        return Object.values(value).some((entry) => hasPartialMarker(entry));
    }
    return false;
}

function resolveRef(openapi, ref) {
    if (typeof ref !== "string" || !ref.startsWith("#/")) {
        return undefined;
    }
    return getAt(
        openapi,
        ref
            .slice(2)
            .split("/")
            .map((part) => part.replaceAll("~1", "/").replaceAll("~0", "~")),
    );
}

function responseHasProblemContent(openapi, response) {
    const resolved = typeof response?.$ref === "string" ? resolveRef(openapi, response.$ref) : response;
    const content = resolved?.content;
    const media = content?.[PROBLEM_CONTENT_TYPE];
    const schema = media?.schema;
    if (typeof schema?.$ref === "string") {
        return isObject(resolveRef(openapi, schema.$ref));
    }
    return isObject(media) && isObject(schema);
}

function operationHasProblemResponse(openapi, operation) {
    const responses = isObject(operation?.responses) ? operation.responses : {};
    return Object.values(responses).some((response) => responseHasProblemContent(openapi, response));
}

function arraysEqual(left, right) {
    return left.length === right.length && left.every((value, index) => value === right[index]);
}

function sameStringSet(left, right) {
    return arraysEqual([...new Set(left)].sort(), [...new Set(right)].sort());
}

function validateRoute(openapi, plan, route, collector) {
    const expectedPath = openApiPathForPattern(route.pattern);
    const { pathItem, operation } = operationFor(openapi, route);
    const label = routeLabel(route);

    if (pathItem === undefined) {
        collector.fail("openapi.route-present", "Plan route must appear in OpenAPI paths", {
            route: label,
            expectedPath,
        });
        return;
    }
    collector.pass("openapi.route-present", "Plan route appears in OpenAPI paths", { route: label, expectedPath });

    if (!isObject(operation)) {
        collector.fail("openapi.method-present", "Plan route method must appear in the OpenAPI path item", {
            route: label,
            expectedPath,
            availableMethods: Object.keys(pathItem).filter((key) => HTTP_METHODS.has(key)),
        });
        return;
    }
    collector.pass("openapi.method-present", "Plan route method appears in OpenAPI", { route: label, expectedPath });

    const expectedPathParams = routeParamNames(route.pattern).sort();
    const actualPathParams = parametersByLocation(operation, "path").map((parameter) => parameter.name).sort();
    const requiredPathParams = parametersByLocation(operation, "path").filter((parameter) => parameter.required !== true);
    if (!sameStringSet(expectedPathParams, actualPathParams) || requiredPathParams.length > 0) {
        collector.fail("openapi.path-param-agreement", "OpenAPI path parameters must exactly match route parameters and be required", {
            route: label,
            expected: expectedPathParams,
            actual: actualPathParams,
            notRequired: requiredPathParams.map((parameter) => parameter.name),
        });
    } else {
        collector.pass("openapi.path-param-agreement", "OpenAPI path parameters agree with Plan route parameters", {
            route: label,
            parameters: expectedPathParams,
        });
    }

    const expectedQuery = expectedQueryParameters(plan, route);
    const actualQuery = parametersByLocation(operation, "query").map((parameter) => parameter.name).sort();
    if (!sameStringSet(expectedQuery, actualQuery)) {
        collector.fail("openapi.query-param-agreement", "OpenAPI query parameters must match statically known Plan query parameters", {
            route: label,
            expected: expectedQuery,
            actual: actualQuery,
        });
    } else {
        collector.pass("openapi.query-param-agreement", "OpenAPI query parameters agree with statically known Plan metadata", {
            route: label,
            parameters: expectedQuery,
        });
    }

    const expectedHeaders = expectedHeaderParameters(route);
    const actualHeaders = parametersByLocation(operation, "header").map((parameter) => parameter.name).sort();
    if (!sameStringSet(expectedHeaders, actualHeaders)) {
        collector.fail("openapi.header-param-agreement", "OpenAPI header parameters must match statically known Plan header parameters", {
            route: label,
            expectedHeaders,
            actualHeaders,
        });
    } else {
        collector.pass("openapi.header-param-agreement", "OpenAPI header parameters agree with statically known Plan metadata", {
            route: label,
            parameters: expectedHeaders,
        });
    }

    if (routeHasKnownBodySchema(plan, route) && !isObject(operation.requestBody)) {
        collector.fail("openapi.request-body-honesty", "OpenAPI must include a request body when the Plan has a statically known body schema", {
            route: label,
        });
    } else if (!routeHasBodyBinding(route) && operation.requestBody !== undefined) {
        collector.fail("openapi.request-body-honesty", "OpenAPI must not invent a request body when the Plan has no body binding", {
            route: label,
        });
    } else if (routeHasBodyBinding(route) && !routeHasKnownBodySchema(plan, route) && !hasPartialMarker(operation.requestBody)) {
        collector.fail("openapi.request-body-honesty", "OpenAPI request bodies with unknown schemas must be marked partial", {
            route: label,
        });
    } else {
        collector.pass("openapi.request-body-honesty", "OpenAPI request body metadata is honest for the Plan route", {
            route: label,
        });
    }

    const responses = isObject(operation.responses) ? operation.responses : {};
    const missingStatuses = staticResponseStatuses(route).filter((status) => !Object.hasOwn(responses, status));
    if (missingStatuses.length > 0) {
        collector.fail("openapi.response-status-agreement", "OpenAPI responses must include statically known response statuses", {
            route: label,
            missingStatuses,
        });
    } else {
        collector.pass("openapi.response-status-agreement", "OpenAPI responses include statically known response statuses", {
            route: label,
            statuses: staticResponseStatuses(route),
        });
    }

    const shouldExposeProblemDetails = routeHasBodyBinding(route) || hasTypedRouteParameter(route.pattern) || route.auth?.required;
    if (shouldExposeProblemDetails && !operationHasProblemResponse(openapi, operation)) {
        collector.fail("openapi.response-status-agreement", "ProblemDetails responses must use application/problem+json where known", {
            route: label,
        });
    } else if (shouldExposeProblemDetails) {
        collector.pass("openapi.response-status-agreement", "ProblemDetails responses use application/problem+json where known", {
            route: label,
        });
    }

    if (route.auth?.required === true) {
        const expectedSchemes = Array.isArray(route.auth.schemes) ? route.auth.schemes : [];
        const actualSchemes = Array.isArray(operation.security)
            ? operation.security.flatMap((entry) => (isObject(entry) ? Object.keys(entry) : []))
            : [];
        const extensionSchemes = Array.isArray(operation["x-slop-auth"]?.schemes)
            ? operation["x-slop-auth"].schemes
            : [];
        if (!sameStringSet(expectedSchemes, actualSchemes) || !sameStringSet(expectedSchemes, extensionSchemes)) {
            collector.fail("openapi.auth-agreement", "Protected Plan routes must preserve OpenAPI security metadata", {
                route: label,
                expectedSchemes,
                actualSchemes,
                extensionSchemes,
            });
        } else {
            collector.pass("openapi.auth-agreement", "OpenAPI auth metadata agrees with protected Plan route metadata", {
                route: label,
                schemes: expectedSchemes,
            });
        }
    } else {
        collector.pass("openapi.auth-agreement", "Route has no required auth metadata to enforce", { route: label });
    }

    if (typeof route.name === "string" && route.name.length > 0 && operation.operationId !== undefined) {
        if (operation.operationId !== route.name) {
            collector.fail("openapi.partial-honesty", "Emitted OpenAPI operationId must stay stable with the Plan route name", {
                route: label,
                expected: route.name,
                actual: operation.operationId,
            });
        }
    }
    if (Array.isArray(route.tags) && operation.tags !== undefined && !sameStringSet(route.tags, operation.tags)) {
        collector.fail("openapi.partial-honesty", "Emitted OpenAPI tags must stay stable with Plan route tags", {
            route: label,
            expected: route.tags,
            actual: operation.tags,
        });
    }

    const mustBePartial = !routeIsPlanComplete(plan, route);
    const isMarkedPartial = operation["x-slop-completeness"] === "partial";
    const hasReasons = Array.isArray(operation["x-slop-missing"]) && operation["x-slop-missing"].length > 0;
    if (mustBePartial && (!isMarkedPartial || !hasReasons)) {
        collector.fail("openapi.partial-honesty", "Incomplete Plan routes must be marked partial with explicit reasons", {
            route: label,
            completeness: operation["x-slop-completeness"],
            missing: operation["x-slop-missing"],
        });
    } else if (!mustBePartial && operation["x-slop-completeness"] === "partial" && !hasReasons) {
        collector.fail("openapi.partial-honesty", "Partial OpenAPI operations must include explicit reasons", {
            route: label,
        });
    } else {
        collector.pass("openapi.partial-honesty", "OpenAPI completeness metadata is honest for the Plan route", {
            route: label,
            completeness: operation["x-slop-completeness"],
        });
    }
}

function validatePolicy(openapi, plan, collector) {
    const policy = openapi?.["x-slop-openapi-policy"];
    if (!isObject(policy)) {
        collector.pass("openapi.policy-counts", "OpenAPI policy extension is absent");
        collector.pass("openapi.policy-mode-honesty", "OpenAPI policy extension is absent");
        return;
    }

    const routesTotalMatches = policy.routesTotal === policy.routesIncluded + policy.routesOmitted;
    const operationsMatch = policy.operationsComplete + policy.operationsPartial === policy.routesIncluded;
    if (!routesTotalMatches || !operationsMatch) {
        collector.fail("openapi.policy-counts", "OpenAPI policy counts must be internally consistent", {
            policy,
        });
    } else {
        collector.pass("openapi.policy-counts", "OpenAPI policy counts are internally consistent", { policy });
    }

    const actualRoutes = new Set(
        publicRoutes(plan).flatMap((route) => [
            `${route.method.toUpperCase()} ${route.pattern}`,
            `${route.method.toUpperCase()} ${openApiPathForPattern(route.pattern)}`,
        ]),
    );
    const badMissing = (Array.isArray(policy.missing) ? policy.missing : []).filter(
        (entry) => !actualRoutes.has(`${String(entry?.method ?? "").toUpperCase()} ${entry?.path}`),
    );
    if (badMissing.length > 0) {
        collector.fail("openapi.policy-counts", "Every OpenAPI missing reason must reference an actual Plan route", {
            missing: badMissing,
        });
    }

    if (policy.optimizations !== undefined && policy.optimizations !== "reported-only") {
        collector.fail("openapi.policy-counts", "OpenAPI optimization candidates must be reported-only", {
            optimizations: policy.optimizations,
        });
    }

    const hasIncompleteMetadata =
        policy.operationsPartial > 0 ||
        policy.routesOmitted > 0 ||
        (Array.isArray(policy.missing) && policy.missing.length > 0);
    if (hasIncompleteMetadata && policy.mode !== "partial") {
        collector.fail("openapi.policy-mode-honesty", "OpenAPI policy mode must be partial when any route metadata is incomplete", {
            mode: policy.mode,
            operationsPartial: policy.operationsPartial,
            routesOmitted: policy.routesOmitted,
            missing: policy.missing,
        });
    } else if (!hasIncompleteMetadata && policy.mode !== "complete") {
        collector.fail("openapi.policy-mode-honesty", "OpenAPI policy mode must be complete only when no partial metadata exists", {
            mode: policy.mode,
        });
    } else {
        collector.pass("openapi.policy-mode-honesty", "OpenAPI policy mode matches completeness metadata", {
            mode: policy.mode,
        });
    }
}

export async function validateOpenApiArtifacts({ plan, openapi, fixture }) {
    const collector = new ContractAssertionCollector({ subsystem: SUBSYSTEM, fixture });
    for (const route of publicRoutes(plan)) {
        validateRoute(openapi, plan, route, collector);
    }
    validatePolicy(openapi, plan, collector);
    return collector.findings;
}

function detectedErrorDetails(rawFindings) {
    return rawFindings
        .filter((finding) => finding.status === "fail" && finding.severity === "error")
        .map((finding) => ({
            invariant: finding.invariant,
            message: finding.message,
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

export async function runOpenApiContract({ repoRoot, tier }) {
    const startedAt = new Date().toISOString();
    const fixtures = await loadFixtures(path.join(repoRoot, "tests/contracts/openapi/fixtures"));
    const findings = [];
    for (const fixture of fixtures) {
        const config = await readFixtureConfig(fixture);
        const plan = await readJson(resolveFixturePath(repoRoot, fixture.root, config.plan));
        const sourceOpenApi = await readJson(resolveFixturePath(repoRoot, fixture.root, config.openapi));
        const openapi = applyMutations(sourceOpenApi, config.mutations);
        const rawFindings = await validateOpenApiArtifacts({ plan, openapi, fixture: fixture.name });
        const detected = errorInvariants(rawFindings);
        if (config.expected === "fail") {
            const expectedList = Array.isArray(config.expectedInvariants) ? config.expectedInvariants : [];
            const expectedInvariants = new Set(expectedList);
            for (const invariant of expectedList) {
                findings.push(
                    detected.includes(invariant)
                        ? expectedFailureFinding(fixture.name, invariant, detected, rawFindings)
                        : failedExpectationFinding(fixture.name, invariant, detected, rawFindings),
                );
            }
            for (const invariant of detected) {
                if (!expectedInvariants.has(invariant)) {
                    findings.push(unexpectedErrorFinding(fixture.name, invariant, expectedList, rawFindings));
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
