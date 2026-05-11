import { EventEmitter } from "./events.js";

const channels = new Map();

class Channel extends EventEmitter {
    constructor(name) {
        super();
        this.name = name;
    }

    publish(message) {
        this.emit("message", message, this.name);
    }

    subscribe(listener) {
        this.on("message", listener);
    }

    unsubscribe(listener) {
        this.removeListener("message", listener);
    }

    hasSubscribers() {
        return this.listenerCount("message") > 0;
    }
}

function channel(name) {
    name = String(name);
    if (!channels.has(name)) {
        channels.set(name, new Channel(name));
    }
    return channels.get(name);
}

function hasSubscribers(name) {
    return channels.get(String(name))?.hasSubscribers() ?? false;
}

export { channel, hasSubscribers };
export default { channel, hasSubscribers };
