import assert from "node:assert/strict";

import {
    Directory,
    File,
    FileHandle,
    FileWatcher,
    Path,
} from "../../stdlib/sloppy/fs.js";
import {
    HttpClient,
    LocalEndpoint,
    NamedPipe,
    NetworkAddress,
    SloppyNetError,
    TcpClient,
    TcpConnection,
    TcpListener,
    UnixSocket,
} from "../../stdlib/sloppy/net.js";
import {
    Environment,
    OsError,
    Process,
    ProcessHandle,
    Signals,
    System,
} from "../../stdlib/sloppy/os.js";
import {
    CancellationController,
    Deadline,
    Time,
} from "../../stdlib/sloppy/time.js";
import {
    ConstantTime,
    Hash,
    Hmac,
    NonCryptoHash,
    Password,
    Random,
    Secret,
} from "../../stdlib/sloppy/crypto.js";
import {
    Base64,
    Base64Url,
    Binary,
    Checksums,
    Compression,
    Hex,
    Text,
} from "../../stdlib/sloppy/codec.js";
import {
    BackgroundService,
    SloppyWorkerError,
    WorkQueue,
    Worker,
    WorkerCancellationController,
    WorkerCancellationSignal,
    WorkerPool,
} from "../../stdlib/sloppy/workers.js";
import {
    t,
    unsafeFfi,
} from "../../stdlib/sloppy/ffi.js";
import {
    Auth,
    BackgroundService as RootBackgroundService,
    Config,
    t as RootFfiTypes,
    HttpClient as RootHttpClient,
    unsafeFfi as RootUnsafeFfi,
    WorkQueue as RootWorkQueue,
    Worker as RootWorker,
    WorkerPool as RootWorkerPool,
} from "../../stdlib/sloppy/index.js";

const documentedSubpathExports = {
    "sloppy/fs": [File, Directory, FileHandle, FileWatcher, Path],
    "sloppy/net": [
        HttpClient,
        TcpClient,
        TcpListener,
        TcpConnection,
        LocalEndpoint,
        UnixSocket,
        NamedPipe,
        NetworkAddress,
        SloppyNetError,
    ],
    "sloppy/os": [System, Environment, Process, ProcessHandle, Signals, OsError],
    "sloppy/time": [Time, Deadline, CancellationController],
    "sloppy/crypto": [Random, Hash, Hmac, Password, ConstantTime, Secret, NonCryptoHash],
    "sloppy/codec": [Base64, Base64Url, Hex, Text, Binary, Compression, Checksums],
    "sloppy/workers": [
        BackgroundService,
        WorkQueue,
        WorkerPool,
        Worker,
        WorkerCancellationController,
        WorkerCancellationSignal,
        SloppyWorkerError,
    ],
    "sloppy/ffi": [t, unsafeFfi],
};

for (const [specifier, exports] of Object.entries(documentedSubpathExports)) {
    for (const value of exports) {
        assert.notEqual(value, undefined, `${specifier} documented export should be present`);
    }
}

assert.equal(RootHttpClient, HttpClient);
assert.equal(typeof Auth.jwtBearer, "function");
assert.equal(typeof Auth.apiKey, "function");
assert.equal(typeof Auth.cookieSession, "function");
assert.equal(typeof Auth.signIn, "function");
assert.equal(typeof Auth.signOut, "function");
assert.equal(typeof Auth.constantTimeEquals, "function");
assert.equal(Auth.constantTimeEquals("same", "same"), true);
assert.equal(Auth.constantTimeEquals("same", "diff"), false);
assert.equal(typeof Config.boolean, "function");
assert.equal(typeof Config.required, "function");
assert.equal(RootBackgroundService, BackgroundService);
assert.equal(RootFfiTypes, t);
assert.equal(RootUnsafeFfi, unsafeFfi);
assert.equal(RootWorkQueue, WorkQueue);
assert.equal(RootWorkerPool, WorkerPool);
assert.equal(RootWorker, Worker);
