import { Text } from "../codec.js";
import { boundedText } from "./redaction.js";
import { Process as SloppyProcess } from "../os.js";

function processOutputText(value) {
    if (value === undefined || value === null) {
        return "";
    }
    if (typeof value === "string") {
        return value;
    }
    if (value instanceof Uint8Array) {
        return Text.utf8.decode(value);
    }
    return String(value);
}

export class DockerCliBackend {
    constructor(options = {}) {
        this.command = options.command ?? "docker";
        this.cwd = options.cwd;
        this.env = options.env;
    }

    async run(args, options = {}) {
        const result = await SloppyProcess.run(this.command, args, {
            cwd: options.cwd ?? this.cwd,
            env: options.env ?? this.env,
            capture: "bytes",
            timeoutMs: options.timeoutMs ?? 30000,
            maxStdoutBytes: options.maxStdoutBytes ?? 1024 * 1024,
            maxStderrBytes: options.maxStderrBytes ?? 1024 * 1024,
        });
        return Object.freeze({
            exitCode: result.exitCode,
            stdout: processOutputText(result.stdout),
            stderr: processOutputText(result.stderr),
            timedOut: result.timedOut === true,
        });
    }
}

export function dockerBackend(options = undefined) {
    if (options?.dockerBackend !== undefined) {
        return options.dockerBackend;
    }
    return new DockerCliBackend(options?.docker);
}

export async function dockerRunOk(backend, args, options = {}) {
    const result = await backend.run(args, options);
    if (result.exitCode !== 0 || result.timedOut === true) {
        const stderr = boundedText(result.stderr || result.stdout);
        throw new Error(`docker ${args[0]} failed with exit code ${result.exitCode}.${stderr.length === 0 ? "" : `\n${stderr}`}`);
    }
    return result;
}

export function dockerUnavailableError(reason) {
    const error = new Error(`SLOPPY_E_TESTSERVICES_DOCKER_UNAVAILABLE: Docker is unavailable for Sloppy TestServices.

Reason:
  ${reason}

Fix:
  Start Docker Desktop or a compatible Docker daemon, ensure the docker CLI is on PATH, then rerun the opt-in TestServices lane.`);
    error.code = "SLOPPY_E_TESTSERVICES_DOCKER_UNAVAILABLE";
    return error;
}

export async function dockerAvailable(options = {}) {
    const backend = dockerBackend(options);
    try {
        const result = await backend.run(["version", "--format", "{{json .}}"], {
            timeoutMs: options.timeoutMs ?? 5000,
            maxStdoutBytes: 64 * 1024,
            maxStderrBytes: 64 * 1024,
        });
        if (result.exitCode !== 0 || result.timedOut === true) {
            const reason = result.timedOut === true
                ? "docker version timed out"
                : boundedText(result.stderr || result.stdout || "docker version failed", 1000);
            return Object.freeze({ ok: false, available: false, reason });
        }
        let version = undefined;
        try {
            version = JSON.parse(result.stdout);
        } catch {
            version = result.stdout.trim();
        }
        return Object.freeze({ ok: true, available: true, reason: undefined, version });
    } catch (error) {
        return Object.freeze({
            ok: false,
            available: false,
            reason: String(error?.message ?? error),
        });
    }
}

export async function dockerRequire(options = {}) {
    const available = await dockerAvailable(options);
    if (available.ok) {
        return available;
    }
    throw dockerUnavailableError(available.reason);
}

export async function ensureImage(backend, image, options) {
    const inspect = await backend.run(["image", "inspect", image], {
        timeoutMs: options.dockerTimeoutMs ?? 15000,
        maxStdoutBytes: 64 * 1024,
        maxStderrBytes: 64 * 1024,
    });
    if (inspect.exitCode === 0) {
        return;
    }
    await dockerRunOk(backend, ["pull", image], {
        timeoutMs: options.pullTimeoutMs ?? 120000,
        maxStdoutBytes: 256 * 1024,
        maxStderrBytes: 256 * 1024,
    });
}

export function parseInspectJson(text) {
    const parsed = JSON.parse(text);
    if (!Array.isArray(parsed) || parsed.length === 0 || parsed[0] === null) {
        throw new Error("docker inspect returned no container metadata.");
    }
    return parsed[0];
}

export function mappedPortFromInspect(metadata, internalPort) {
    const ports = metadata?.NetworkSettings?.Ports;
    const entries = ports?.[`${internalPort}/tcp`];
    if (!Array.isArray(entries) || entries.length === 0) {
        throw new Error(`docker inspect did not report a mapped host port for ${internalPort}/tcp.`);
    }
    const hostPort = Number(entries[0].HostPort);
    if (!Number.isInteger(hostPort) || hostPort < 1 || hostPort > 65535) {
        throw new Error(`docker inspect returned an invalid host port for ${internalPort}/tcp.`);
    }
    return hostPort;
}

export async function inspectContainer(backend, containerId, internalPort, options) {
    const result = await dockerRunOk(backend, ["inspect", containerId], {
        timeoutMs: options.dockerTimeoutMs ?? 15000,
        maxStdoutBytes: 256 * 1024,
        maxStderrBytes: 64 * 1024,
    });
    const metadata = parseInspectJson(result.stdout);
    return { metadata, port: mappedPortFromInspect(metadata, internalPort) };
}
