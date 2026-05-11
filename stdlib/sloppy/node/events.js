const onceOriginal = Symbol("onceOriginal");

class EventEmitter {
    constructor() {
        this._events = new Map();
    }

    on(name, listener) {
        if (typeof listener !== "function") {
            throw new TypeError("EventEmitter listener must be a function.");
        }
        const listeners = this._events.get(name) ?? [];
        listeners.push(listener);
        this._events.set(name, listeners);
        return this;
    }

    addListener(name, listener) {
        return this.on(name, listener);
    }

    prependListener(name, listener) {
        if (typeof listener !== "function") {
            throw new TypeError("EventEmitter listener must be a function.");
        }
        const listeners = this._events.get(name) ?? [];
        listeners.unshift(listener);
        this._events.set(name, listeners);
        return this;
    }

    once(name, listener) {
        if (typeof listener !== "function") {
            throw new TypeError("EventEmitter listener must be a function.");
        }
        const wrapped = (...args) => {
            this.off(name, wrapped);
            listener(...args);
        };
        wrapped[onceOriginal] = listener;
        return this.on(name, wrapped);
    }

    off(name, listener) {
        const listeners = this._events.get(name) ?? [];
        this._events.set(
            name,
            listeners.filter((candidate) => candidate !== listener && candidate[onceOriginal] !== listener),
        );
        return this;
    }

    removeListener(name, listener) {
        return this.off(name, listener);
    }

    emit(name, ...args) {
        const listeners = [...(this._events.get(name) ?? [])];
        for (const listener of listeners) {
            listener(...args);
        }
        return listeners.length > 0;
    }

    listenerCount(name) {
        return (this._events.get(name) ?? []).length;
    }

    removeAllListeners(name = undefined) {
        if (name === undefined) {
            this._events.clear();
        } else {
            this._events.delete(name);
        }
        return this;
    }
}

function once(emitter, name) {
    return new Promise((resolve) => {
        emitter.once(name, (...args) => resolve(args));
    });
}

async function* on(emitter, name) {
    const queue = [];
    let notify = undefined;
    const listener = (...args) => {
        queue.push(args);
        if (notify !== undefined) {
            notify();
            notify = undefined;
        }
    };
    emitter.on(name, listener);
    try {
        while (true) {
            if (queue.length === 0) {
                await new Promise((resolve) => {
                    notify = resolve;
                });
            }
            while (queue.length > 0) {
                yield queue.shift();
            }
        }
    } finally {
        emitter.removeListener(name, listener);
    }
}

export { EventEmitter, on, once };
export default { EventEmitter, on, once };
