function isatty() {
    return false;
}

class ReadStream {
    constructor() {
        this.isTTY = false;
    }
}

class WriteStream {
    constructor() {
        this.isTTY = false;
    }
}

export { isatty, ReadStream, WriteStream };
export default { isatty, ReadStream, WriteStream };
