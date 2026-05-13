import assert from "node:assert/strict";

export async function assertMemoryBackplaneConformance(factory) {
    const backplane = factory();
    const first = [];
    const second = [];
    const third = [];

    for (const method of [
        "connect",
        "disconnect",
        "join",
        "leave",
        "leaveAll",
        "groups",
        "groupSize",
        "send",
        "broadcast",
        "presenceSet",
        "presenceGet",
        "presenceInGroup",
        "dispose",
        "health",
    ]) {
        assert.equal(typeof backplane[method], "function", `memory backplane implements ${method}()`);
    }

    await backplane.connect({ connectionId: "c1", userId: "u1", connectedAt: "t1", send: async (envelope) => first.push(envelope) });
    await backplane.connect({ connectionId: "c2", userId: "u2", connectedAt: "t2", send: async (envelope) => second.push(envelope) });
    await backplane.connect({ connectionId: "c3", userId: "u3", connectedAt: "t3", send: async (envelope) => third.push(envelope) });
    await backplane.join("c1", "room:one");
    await backplane.join("c2", "room:one");
    await backplane.join("c3", "room:one");

    assert.deepEqual(await backplane.groups("c1"), ["room:one"]);
    assert.equal(await backplane.groupSize("room:one"), 3);

    await backplane.send("c1", { type: "direct", data: "one" });
    assert.deepEqual(first, [{ type: "direct", data: "one" }]);

    await backplane.broadcast("room:one", { type: "fanout", data: 1 }, { senderId: "c1", exceptSelf: true });
    assert.deepEqual(first, [{ type: "direct", data: "one" }]);
    assert.deepEqual(second, [{ type: "fanout", data: 1 }]);
    assert.deepEqual(third, [{ type: "fanout", data: 1 }]);

    await backplane.broadcast("room:one", { type: "fanout", data: 2 }, { except: ["c2"] });
    assert.deepEqual(first, [{ type: "direct", data: "one" }, { type: "fanout", data: 2 }]);
    assert.deepEqual(second, [{ type: "fanout", data: 1 }]);
    assert.deepEqual(third, [{ type: "fanout", data: 1 }, { type: "fanout", data: 2 }]);

    const presence = await backplane.presenceSet("c1", { metadata: { status: "online" } });
    assert.equal(presence.userId, "u1");
    assert.deepEqual(presence.groups, ["room:one"]);
    assert.deepEqual((await backplane.presenceGet("c1")).metadata, { status: "online" });
    assert.equal((await backplane.presenceInGroup("room:one")).length, 1);

    assert.deepEqual(await backplane.leaveAll("c1"), { count: 1 });
    assert.deepEqual(await backplane.groups("c1"), []);
    assert.equal(await backplane.groupSize("room:one"), 2);

    await backplane.disconnect("c2");
    assert.equal(await backplane.groupSize("room:one"), 1);

    await backplane.connect({ connectionId: "reuse", userId: "old", connectedAt: "old", send: async () => {} });
    await backplane.join("reuse", "room:stale");
    await backplane.presenceSet("reuse", { metadata: { version: "old" } });
    assert.equal(await backplane.groupSize("room:stale"), 1);
    await backplane.connect({ connectionId: "reuse", userId: "new", connectedAt: "new", send: async () => {} });
    assert.equal(await backplane.groupSize("room:stale"), 0);
    assert.equal(await backplane.presenceGet("reuse"), undefined);

    await backplane.dispose();
    await backplane.disconnect("reuse");
    assert.equal(typeof backplane.health().status, "string");
}
