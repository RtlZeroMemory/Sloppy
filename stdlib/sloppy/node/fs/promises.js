export {
    promises as default,
    promises,
} from "../fs.js";

import { promises } from "../fs.js";

const {
    access,
    appendFile,
    copyFile,
    lstat,
    mkdir,
    mkdtemp,
    readdir,
    readFile,
    readlink,
    realpath,
    rename,
    rm,
    stat,
    symlink,
    unlink,
    writeFile,
} = promises;

export { access, appendFile, copyFile, lstat, mkdir, mkdtemp, readdir, readFile, readlink, realpath, rename, rm, stat, symlink, unlink, writeFile };
