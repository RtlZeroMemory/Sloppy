const O_RDONLY = 0;
const O_WRONLY = 1;
const O_RDWR = 2;
const S_IFMT = 0o170000;
const S_IFREG = 0o100000;
const S_IFDIR = 0o040000;

export { O_RDONLY, O_RDWR, O_WRONLY, S_IFDIR, S_IFMT, S_IFREG };
export default { O_RDONLY, O_RDWR, O_WRONLY, S_IFDIR, S_IFMT, S_IFREG };
