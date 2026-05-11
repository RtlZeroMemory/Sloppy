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
        this._pendingWrites = new Set();
        this._finished = Promise.resolve(this);
    }

    _invokeWrite(chunk) {
        return new Promise((resolve, reject) => {
            try {
                if (this._write) {
                    if (this._write.length >= 3) {
                        this._write(chunk, undefined, (error) => error ? reject(error) : resolve());
                    } else {
                        Promise.resolve(this._write(chunk)).then(resolve, reject);
                    }
                } else {
                    this.chunks.push(chunk);
                    resolve();
                }
            } catch (error) {
                reject(error);
            }
        });
    }

    _trackWrite(promise) {
        const tracked = Promise.resolve(promise).finally(() => this._pendingWrites.delete(tracked));
        this._pendingWrites.add(tracked);
        return tracked;
    }

    write(chunk, encodingOrCallback = undefined, callback = undefined) {
        const done = typeof encodingOrCallback === "function" ? encodingOrCallback : callback;
        this._trackWrite(this._invokeWrite(chunk)).then(
            () => {
                if (typeof done === "function") {
                    done();
                }
                this.emit("drain");
            },
            (error) => {
                if (typeof done === "function") {
                    done(error);
                }
                this.emit("error", error);
            },
        );
        return true;
    }

    end(chunk = undefined, encodingOrCallback = undefined, callback = undefined) {
        if (typeof chunk === "function") {
            callback = chunk;
            chunk = undefined;
        } else if (typeof encodingOrCallback === "function") {
            callback = encodingOrCallback;
        }
        if (chunk !== undefined) {
            this.write(chunk);
        }
        if (typeof callback === "function") {
            this.once("finish", callback);
        }
        this._finished = Promise.all([...this._pendingWrites]).then(
            () => {
                this.emit("finish");
                return this;
            },
            (error) => {
                this.emit("error", error);
                throw error;
            },
        );
        return this;
    }

    get finished() {
        return this._finished;
    }
}

class PassThrough extends Writable {
    write(chunk, encodingOrCallback = undefined, callback = undefined) {
        super.write(chunk, encodingOrCallback, callback);
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
        await destination.finished;
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
