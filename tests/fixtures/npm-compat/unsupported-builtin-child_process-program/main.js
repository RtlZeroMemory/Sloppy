import { spawn } from "node:child_process";

export function main() {
    spawn("noop", []);
}
