export {
    promises as default,
    promises,
} from "../fs.js";

import { promises } from "../fs.js";

const {
    access,
    appendFile,
    mkdir,
    readdir,
    readFile,
    rm,
    stat,
    unlink,
    writeFile,
} = promises;

export { access, appendFile, mkdir, readdir, readFile, rm, stat, unlink, writeFile };
