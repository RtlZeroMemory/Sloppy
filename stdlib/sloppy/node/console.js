const runtimeConsole = globalThis.console;

const Console = runtimeConsole?.Console ?? class Console {
    log(...values) {
        runtimeConsole?.log?.(...values);
    }

    error(...values) {
        runtimeConsole?.error?.(...values);
    }

    warn(...values) {
        (runtimeConsole?.warn ?? runtimeConsole?.log)?.(...values);
    }

    info(...values) {
        (runtimeConsole?.info ?? runtimeConsole?.log)?.(...values);
    }

    debug(...values) {
        (runtimeConsole?.debug ?? runtimeConsole?.log)?.(...values);
    }
};

const log = runtimeConsole?.log?.bind(runtimeConsole) ?? (() => {});
const error = runtimeConsole?.error?.bind(runtimeConsole) ?? (() => {});
const warn = runtimeConsole?.warn?.bind(runtimeConsole) ?? log;
const info = runtimeConsole?.info?.bind(runtimeConsole) ?? log;
const debug = runtimeConsole?.debug?.bind(runtimeConsole) ?? log;
const consoleModule = runtimeConsole ?? { Console, debug, error, info, log, warn };

export { Console, debug, error, info, log, warn };
export default consoleModule;
