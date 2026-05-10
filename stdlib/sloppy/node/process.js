import { Environment, Process } from "../os.js";

const env = new Proxy(Object.create(null), {
    get(_target, key) {
        return typeof key === "string" ? Environment.get(key) : undefined;
    },
    ownKeys() {
        return Object.keys(Environment.list?.() ?? {});
    },
});

const argv = [];
const cwd = () => ".";
const exit = (code = 0) => {
    throw new Error(`SLOPPY_E_PROCESS_EXIT_UNSUPPORTED: process.exit(${code}) is not supported.`);
};

export { argv, cwd, env, exit, Process };
export default { argv, cwd, env, exit };
