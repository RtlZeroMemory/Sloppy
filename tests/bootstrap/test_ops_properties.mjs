import assert from "node:assert/strict";

import { Health, Metrics } from "../../stdlib/sloppy/index.js";
import { escapePrometheusText } from "../../stdlib/sloppy/metrics.js";

function makePrng(seed) {
    let state = seed >>> 0;
    return () => {
        state ^= state << 13;
        state ^= state >>> 17;
        state ^= state << 5;
        return state >>> 0;
    };
}

function makeLabelValue(next, length) {
    let output = "";
    for (let index = 0; index < length; index += 1) {
        const value = next() % 8;
        if (value === 0) {
            output += "\\";
        } else if (value === 1) {
            output += "\"";
        } else if (value === 2) {
            output += "\n";
        } else {
            output += String.fromCodePoint(0x20 + (next() % 0x5f));
        }
    }
    return output;
}

function escapeRegExp(text) {
    return String(text).replace(/[\\^$.*+?()[\]{}|]/gu, "\\$&");
}

for (let seed = 1; seed <= 128; seed += 1) {
    const next = makePrng(0x51510f5 ^ seed);
    const registry = Metrics.createRegistry();
    const label = makeLabelValue(next, next() % 48);
    registry.counter("ops_property_total").inc({ route: `/items/{id}`, value: label });
    const prometheus = registry.renderPrometheus();
    assert.match(prometheus, /ops_property_total\{route="\/items\/\{id\}",value="/);
    assert.match(prometheus, new RegExp(`value="${escapeRegExp(escapePrometheusText(label))}"`, "u"));
    assert.doesNotThrow(() => JSON.parse(registry.renderJson()));
}

for (let seed = 1; seed <= 128; seed += 1) {
    const next = makePrng(0x5afe0f5 ^ seed);
    const payload = {
        safe: makeLabelValue(next, next() % 32),
        password: makeLabelValue(next, 32),
        nested: {
            apiKey: makeLabelValue(next, 32),
            token: makeLabelValue(next, 32),
        },
    };
    const redacted = Health.redact(payload);
    assert.equal(redacted.password, "[redacted]");
    assert.equal(redacted.nested.apiKey, "[redacted]");
    assert.equal(redacted.nested.token, "[redacted]");
    assert.equal(redacted.safe, payload.safe);
}
