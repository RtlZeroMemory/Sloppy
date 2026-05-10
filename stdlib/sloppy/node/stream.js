import { EventEmitter } from "./events.js";

class Stream extends EventEmitter {}

class Readable extends Stream {
    static from(iterable) {
        const readable = new Readable();
        readable._iterable = iterable;
        return readable;
    }

    async *[Symbol.asyncIterator]() {
        for await (const chunk of this._iterable ?? []) {
            yield chunk;
        }
    }
}

class Writable extends Stream {
    constructor(options = {}) {
        super();
        this._write = typeof options.write === "function" ? options.write : undefined;
        this.chunks = [];
    }

    write(chunk, callback = undefined) {
        Promise.resolve(this._write ? this._write(chunk) : this.chunks.push(chunk)).then(
            () => {
                if (typeof callback === "function") {
                    callback();
                }
                this.emit("drain");
            },
            (error) => {
                if (typeof callback === "function") {
                    callback(error);
                }
                this.emit("error", error);
            },
        );
        return true;
    }

    end(chunk = undefined, callback = undefined) {
        if (chunk !== undefined) {
            this.write(chunk);
        }
        if (typeof callback === "function") {
            this.once("finish", callback);
        }
        this.emit("finish");
        return this;
    }
}

class PassThrough extends Writable {
    write(chunk, callback = undefined) {
        super.write(chunk, callback);
        this.emit("data", chunk);
        return true;
    }
}

async function pipeline(source, destination, callback = undefined) {
    try {
        for await (const chunk of source) {
            destination.write(chunk);
        }
        destination.end();
        if (typeof callback === "function") {
            callback();
        }
        return destination;
    } catch (error) {
        if (typeof callback === "function") {
            callback(error);
            return destination;
        }
        throw error;
    }
}

export { PassThrough, Readable, Stream, Writable, pipeline };
export default { PassThrough, Readable, Stream, Writable, pipeline };
