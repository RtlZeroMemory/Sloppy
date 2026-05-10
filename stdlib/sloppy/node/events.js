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

    once(name, listener) {
        const wrapped = (...args) => {
            this.off(name, wrapped);
            listener(...args);
        };
        return this.on(name, wrapped);
    }

    off(name, listener) {
        const listeners = this._events.get(name) ?? [];
        this._events.set(name, listeners.filter((candidate) => candidate !== listener));
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
}

function once(emitter, name) {
    return new Promise((resolve) => {
        emitter.once(name, (...args) => resolve(args));
    });
}

export { EventEmitter, once };
export default { EventEmitter, once };
