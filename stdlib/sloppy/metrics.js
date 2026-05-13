const METRIC_NAME_PATTERN = /^[A-Za-z_:][A-Za-z0-9_:.-]*$/u;
const LABEL_NAME_PATTERN = /^[A-Za-z_][A-Za-z0-9_]*$/u;
const DEFAULT_HISTOGRAM_BUCKETS = Object.freeze([1, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000]);
const DEFAULT_MAX_LABELSETS = 128;

function isPlainObject(value) {
    if (value === null || typeof value !== "object" || Array.isArray(value)) {
        return false;
    }
    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
}

function assertMetricName(name) {
    if (typeof name !== "string" || !METRIC_NAME_PATTERN.test(name)) {
        throw new TypeError("Sloppy metric name must match [A-Za-z_:][A-Za-z0-9_:.-]*.");
    }
}

function assertLabelName(name) {
    if (typeof name !== "string" || !LABEL_NAME_PATTERN.test(name)) {
        throw new TypeError("Sloppy metric label names must match [A-Za-z_][A-Za-z0-9_]*.");
    }
}

function assertFiniteNumber(value, subject) {
    if (typeof value !== "number" || !Number.isFinite(value)) {
        throw new TypeError(`Sloppy metric ${subject} must be a finite number.`);
    }
}

function normalizeDescription(description) {
    if (description === undefined) {
        return "";
    }
    if (typeof description !== "string") {
        throw new TypeError("Sloppy metric description must be a string.");
    }
    return description;
}

function normalizeLabels(labels = undefined) {
    if (labels === undefined || labels === null) {
        return Object.freeze({});
    }
    if (!isPlainObject(labels)) {
        throw new TypeError("Sloppy metric labels must be a plain object.");
    }
    const normalized = {};
    for (const [name, value] of Object.entries(labels).sort(([left], [right]) => left.localeCompare(right))) {
        assertLabelName(name);
        if (value === undefined || value === null) {
            normalized[name] = "";
        } else if (typeof value === "string" || typeof value === "number" || typeof value === "boolean") {
            normalized[name] = String(value);
        } else {
            throw new TypeError("Sloppy metric label values must be strings, numbers, booleans, null, or undefined.");
        }
        if (normalized[name].length > 256) {
            throw new TypeError("Sloppy metric label values must be at most 256 characters.");
        }
    }
    return Object.freeze(normalized);
}

function labelKey(labels) {
    const entries = Object.entries(labels);
    return entries.length === 0 ? "" : JSON.stringify(entries);
}

function normalizeMetricOptions(options = undefined) {
    if (options === undefined) {
        return Object.freeze({
            description: "",
            maxLabelSets: DEFAULT_MAX_LABELSETS,
        });
    }
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy metric options must be a plain object.");
    }
    const maxLabelSets = options.maxLabelSets ?? DEFAULT_MAX_LABELSETS;
    if (!Number.isInteger(maxLabelSets) || maxLabelSets < 1 || maxLabelSets > 100000) {
        throw new TypeError("Sloppy metric maxLabelSets must be an integer from 1 to 100000.");
    }
    return Object.freeze({
        description: normalizeDescription(options.description ?? options.help),
        maxLabelSets,
    });
}

function normalizeHistogramOptions(options = undefined) {
    const base = normalizeMetricOptions(options);
    const sourceBuckets = options?.buckets ?? DEFAULT_HISTOGRAM_BUCKETS;
    if (!Array.isArray(sourceBuckets) || sourceBuckets.length === 0) {
        throw new TypeError("Sloppy histogram buckets must be a non-empty number array.");
    }
    const buckets = [...sourceBuckets].map((bucket) => {
        assertFiniteNumber(bucket, "bucket");
        if (bucket <= 0) {
            throw new TypeError("Sloppy histogram buckets must be positive.");
        }
        return bucket;
    }).sort((left, right) => left - right);
    for (let index = 1; index < buckets.length; index += 1) {
        if (buckets[index] === buckets[index - 1]) {
            throw new TypeError("Sloppy histogram buckets must be unique.");
        }
    }
    return Object.freeze({
        ...base,
        buckets: Object.freeze(buckets),
    });
}

function escapePrometheusText(text) {
    return String(text).replace(/\\/gu, "\\\\").replace(/\n/gu, "\\n").replace(/"/gu, "\\\"");
}

function metricTypeName(type) {
    return type === "histogram" ? "histogram" : type;
}

function prometheusMetricName(name) {
    return name.replace(/[.-]/gu, "_");
}

class MetricHandle {
    constructor(registry, name, type, options) {
        this.registry = registry;
        this.name = name;
        this.type = type;
        this.description = options.description;
        this.maxLabelSets = options.maxLabelSets;
        this.buckets = options.buckets ?? undefined;
        this.series = new Map();
        Object.seal(this);
    }

    _series(labels = undefined) {
        const normalizedLabels = normalizeLabels(labels);
        const key = labelKey(normalizedLabels);
        let series = this.series.get(key);
        if (series !== undefined) {
            return series;
        }
        if (this.series.size >= this.maxLabelSets) {
            this.registry._cardinalityDrop();
            return undefined;
        }
        series = this.type === "histogram"
            ? {
                labels: normalizedLabels,
                count: 0,
                sum: 0,
                buckets: this.buckets.map((le) => ({ le, count: 0 })),
            }
            : {
                labels: normalizedLabels,
                value: 0,
            };
        this.series.set(key, series);
        return series;
    }

    inc(labelsOrValue = undefined, maybeValue = undefined) {
        if (this.type !== "counter" && this.type !== "gauge") {
            throw new TypeError("Sloppy metric inc is only available on counters and gauges.");
        }
        const labels = typeof labelsOrValue === "number" ? undefined : labelsOrValue;
        const amount = typeof labelsOrValue === "number" ? labelsOrValue : maybeValue ?? 1;
        assertFiniteNumber(amount, "increment");
        if (this.type === "counter" && amount < 0) {
            throw new TypeError("Sloppy counter increments must be non-negative.");
        }
        const series = this._series(labels);
        if (series !== undefined) {
            series.value += amount;
        }
        return this;
    }

    dec(labelsOrValue = undefined, maybeValue = undefined) {
        if (this.type !== "gauge") {
            throw new TypeError("Sloppy metric dec is only available on gauges.");
        }
        const labels = typeof labelsOrValue === "number" ? undefined : labelsOrValue;
        const amount = typeof labelsOrValue === "number" ? labelsOrValue : maybeValue ?? 1;
        assertFiniteNumber(amount, "decrement");
        const series = this._series(labels);
        if (series !== undefined) {
            series.value -= amount;
        }
        return this;
    }

    set(labelsOrValue, maybeValue = undefined) {
        if (this.type !== "gauge") {
            throw new TypeError("Sloppy metric set is only available on gauges.");
        }
        const labels = typeof labelsOrValue === "number" ? undefined : labelsOrValue;
        const value = typeof labelsOrValue === "number" ? labelsOrValue : maybeValue;
        assertFiniteNumber(value, "value");
        const series = this._series(labels);
        if (series !== undefined) {
            series.value = value;
        }
        return this;
    }

    observe(labelsOrValue, maybeValue = undefined) {
        if (this.type !== "histogram") {
            throw new TypeError("Sloppy metric observe is only available on histograms.");
        }
        const labels = typeof labelsOrValue === "number" ? undefined : labelsOrValue;
        const value = typeof labelsOrValue === "number" ? labelsOrValue : maybeValue;
        assertFiniteNumber(value, "observation");
        if (value < 0) {
            throw new TypeError("Sloppy histogram observations must be non-negative.");
        }
        const series = this._series(labels);
        if (series !== undefined) {
            series.count += 1;
            series.sum += value;
            for (const bucket of series.buckets) {
                if (value <= bucket.le) {
                    bucket.count += 1;
                }
            }
        }
        return this;
    }

    timer(labels = undefined) {
        const started = nowMs();
        let stopped = false;
        return () => {
            if (stopped) {
                return 0;
            }
            stopped = true;
            const elapsed = Math.max(0, nowMs() - started);
            this.observe(labels, elapsed);
            return elapsed;
        };
    }

    async measure(callback, labels = undefined) {
        if (typeof callback !== "function") {
            throw new TypeError("Sloppy histogram measure requires a callback.");
        }
        const stop = this.timer(labels);
        try {
            return await callback();
        } finally {
            stop();
        }
    }

    reset() {
        this.series.clear();
    }

    snapshot() {
        const series = [...this.series.values()].map((entry) => {
            if (this.type === "histogram") {
                return Object.freeze({
                    labels: entry.labels,
                    count: entry.count,
                    sum: entry.sum,
                    buckets: Object.freeze(entry.buckets.map((bucket) => Object.freeze({ ...bucket }))),
                });
            }
            return Object.freeze({
                labels: entry.labels,
                value: entry.value,
            });
        }).sort((left, right) => labelKey(left.labels).localeCompare(labelKey(right.labels)));
        return Object.freeze({
            name: this.name,
            type: this.type,
            description: this.description,
            buckets: this.buckets,
            series: Object.freeze(series),
        });
    }
}

function nowMs() {
    if (globalThis.performance !== undefined && typeof globalThis.performance.now === "function") {
        return globalThis.performance.now();
    }
    return Date.now();
}

function metricLabelsText(labels, extra = undefined) {
    const entries = Object.entries({ ...labels, ...(extra ?? {}) });
    if (entries.length === 0) {
        return "";
    }
    return `{${entries.map(([name, value]) => `${name}="${escapePrometheusText(value)}"`).join(",")}}`;
}

function renderPrometheusMetric(metric) {
    const lines = [];
    const name = prometheusMetricName(metric.name);
    lines.push(`# HELP ${name} ${escapePrometheusText(metric.description || metric.name)}`);
    lines.push(`# TYPE ${name} ${metricTypeName(metric.type)}`);
    for (const series of metric.series) {
        if (metric.type === "histogram") {
            for (const bucket of series.buckets) {
                lines.push(`${name}_bucket${metricLabelsText(series.labels, { le: String(bucket.le) })} ${bucket.count}`);
            }
            lines.push(`${name}_bucket${metricLabelsText(series.labels, { le: "+Inf" })} ${series.count}`);
            lines.push(`${name}_sum${metricLabelsText(series.labels)} ${series.sum}`);
            lines.push(`${name}_count${metricLabelsText(series.labels)} ${series.count}`);
        } else {
            lines.push(`${name}${metricLabelsText(series.labels)} ${series.value}`);
        }
    }
    return lines;
}

function createMetricsRegistry(options = undefined) {
    if (options !== undefined && !isPlainObject(options)) {
        throw new TypeError("Sloppy Metrics registry options must be a plain object.");
    }
    const metrics = new Map();
    let cardinalityDrops = 0;
    const registry = {
        _cardinalityDrop() {
            cardinalityDrops += 1;
        },
        counter(name, metricOptions = undefined) {
            return registerMetric(registry, metrics, name, "counter", normalizeMetricOptions(metricOptions));
        },
        gauge(name, metricOptions = undefined) {
            return registerMetric(registry, metrics, name, "gauge", normalizeMetricOptions(metricOptions));
        },
        histogram(name, metricOptions = undefined) {
            return registerMetric(registry, metrics, name, "histogram", normalizeHistogramOptions(metricOptions));
        },
        timer(name, metricOptions = undefined) {
            return registry.histogram(name, metricOptions);
        },
        get(name) {
            return metrics.get(name);
        },
        snapshot() {
            const snapshot = [...metrics.values()].map((metric) => metric.snapshot())
                .sort((left, right) => left.name.localeCompare(right.name));
            const output = {
                metrics: Object.freeze(snapshot),
                cardinalityDrops,
            };
            return Object.freeze(output);
        },
        reset() {
            for (const metric of metrics.values()) {
                metric.reset();
            }
            cardinalityDrops = 0;
        },
        toJSON() {
            return registry.snapshot();
        },
        renderJson() {
            return JSON.stringify(registry.snapshot(), null, 2);
        },
        renderPrometheus() {
            return renderPrometheus(registry.snapshot());
        },
    };
    return Object.freeze(registry);
}

function registerMetric(registry, metrics, name, type, options) {
    assertMetricName(name);
    const existing = metrics.get(name);
    if (existing !== undefined) {
        if (existing.type !== type) {
            throw new TypeError(`Sloppy metric '${name}' is already registered as ${existing.type}.`);
        }
        return existing;
    }
    const metric = new MetricHandle(registry, name, type, options);
    metrics.set(name, metric);
    return metric;
}

function renderPrometheus(snapshot) {
    const lines = [];
    for (const metric of snapshot.metrics) {
        lines.push(...renderPrometheusMetric(metric));
    }
    lines.push("# HELP sloppy_metrics_cardinality_drops_total Dropped metric labelsets rejected by cardinality guards.");
    lines.push("# TYPE sloppy_metrics_cardinality_drops_total counter");
    lines.push(`sloppy_metrics_cardinality_drops_total ${snapshot.cardinalityDrops}`);
    return `${lines.join("\n")}\n`;
}

const defaultRegistry = createMetricsRegistry();

const Metrics = Object.freeze({
    createRegistry: createMetricsRegistry,
    get defaultRegistry() {
        return defaultRegistry;
    },
    counter(name, options = undefined) {
        return defaultRegistry.counter(name, options);
    },
    gauge(name, options = undefined) {
        return defaultRegistry.gauge(name, options);
    },
    histogram(name, options = undefined) {
        return defaultRegistry.histogram(name, options);
    },
    timer(name, options = undefined) {
        return defaultRegistry.timer(name, options);
    },
    snapshot() {
        return defaultRegistry.snapshot();
    },
    reset() {
        defaultRegistry.reset();
    },
    renderJson(registry = defaultRegistry) {
        return registry.renderJson();
    },
    renderPrometheus(registry = defaultRegistry) {
        return registry.renderPrometheus();
    },
});

export { Metrics, createMetricsRegistry, escapePrometheusText, renderPrometheus };
