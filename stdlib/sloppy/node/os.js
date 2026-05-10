import { Environment, System } from "../os.js";

const EOL = "\n";

function platform() {
    return System?.platform ?? "sloppy";
}

function arch() {
    return System?.arch ?? "x64";
}

function tmpdir() {
    return Environment.get?.("TMPDIR") ?? Environment.get?.("TEMP") ?? ".";
}

function homedir() {
    return Environment.get?.("HOME") ?? Environment.get?.("USERPROFILE") ?? ".";
}

export { arch, EOL, homedir, platform, tmpdir };
export default { arch, EOL, homedir, platform, tmpdir };
