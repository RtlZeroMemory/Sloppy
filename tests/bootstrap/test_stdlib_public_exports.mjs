import assert from "node:assert/strict";

import {
    Directory,
    File,
    FileHandle,
    FileWatcher,
    Path,
} from "../../stdlib/sloppy/fs.js";
import {
    Http,
    HttpClientFactory,
    HttpError,
    SloppyHttpClientError,
    TestHttp,
} from "../../stdlib/sloppy/http.js";
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
    SloppyWebhookError,
    TestWebhooks,
    Webhooks,
} from "../../stdlib/sloppy/webhooks.js";
import {
    t,
    unsafeFfi,
} from "../../stdlib/sloppy/ffi.js";
import {
    Cache as CacheSubpath,
    SloppyCacheError,
} from "../../stdlib/sloppy/cache.js";
import {
    Redis as RedisSubpath,
    SloppyRedisError as RedisSubpathError,
} from "../../stdlib/sloppy/redis.js";
import {
    column,
    orm,
    relation,
    table,
} from "../../stdlib/sloppy/orm.js";
import {
    Auth,
    BackgroundService as RootBackgroundService,
    Cache,
    Config,
    FakeClock,
    Health,
    Http as RootHttp,
    HttpClientFactory as RootHttpClientFactory,
    HttpError as RootHttpError,
    SloppyHttpClientError as RootSloppyHttpClientError,
    Metrics,
    SloppyCacheError as RootSloppyCacheError,
    RateLimit,
    Realtime,
    Redis,
    SloppyRedisError,
    t as RootFfiTypes,
    HttpClient as RootHttpClient,
    SloppyRealtimeError,
    TestHttp as RootTestHttp,
    TestData,
    TestHost,
    TestServices,
    column as RootColumn,
    orm as RootOrm,
    relation as RootRelation,
    table as RootTable,
    SloppyRateLimitError,
    TestWebhooks as RootTestWebhooks,
    Webhooks as RootWebhooks,
    unsafeFfi as RootUnsafeFfi,
    WorkQueue as RootWorkQueue,
    Worker as RootWorker,
    WorkerPool as RootWorkerPool,
} from "../../stdlib/sloppy/index.js";

const documentedSubpathExports = {
    "sloppy/fs": [File, Directory, FileHandle, FileWatcher, Path],
    "sloppy/http": [Http, HttpClientFactory, HttpError, SloppyHttpClientError, TestHttp],
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
    "sloppy/webhooks": [Webhooks, SloppyWebhookError, TestWebhooks],
    "sloppy/ffi": [t, unsafeFfi],
    "sloppy/cache": [CacheSubpath, SloppyCacheError],
    "sloppy/redis": [RedisSubpath, RedisSubpathError],
    "sloppy/orm": [orm, table, column, relation],
};

for (const [specifier, exports] of Object.entries(documentedSubpathExports)) {
    for (const value of exports) {
        assert.notEqual(value, undefined, `${specifier} documented export should be present`);
    }
}

assert.equal(RootHttpClient, HttpClient);
assert.equal(Cache, CacheSubpath);
assert.equal(RootSloppyCacheError, SloppyCacheError);
assert.equal(typeof Cache.memory, "function");
assert.equal(typeof Cache.hybrid, "function");
assert.equal(typeof Cache.token, "function");
assert.equal(typeof Cache.redis, "function");
assert.equal(Redis, RedisSubpath);
assert.equal(SloppyRedisError, RedisSubpathError);
assert.equal(typeof Redis.client, "function");
assert.equal(typeof Redis.locks, "function");
assert.equal(typeof SloppyRedisError, "function");
assert.equal(RootHttp, Http);
assert.equal(RootHttpClientFactory, HttpClientFactory);
assert.equal(RootHttpError, HttpError);
assert.equal(RootSloppyHttpClientError, SloppyHttpClientError);
assert.equal(RootTestHttp, TestHttp);
assert.equal(typeof Auth.jwtBearer, "function");
assert.equal(typeof Auth.apiKey, "function");
assert.equal(typeof Auth.cookieSession, "function");
assert.equal(typeof Auth.policy, "function");
assert.equal(typeof Auth.signIn, "function");
assert.equal(typeof Auth.signOut, "function");
assert.equal(typeof Auth.sessionStore.memory, "function");
assert.equal(typeof Auth.sessionStore.dataProvider, "function");
assert.equal(typeof Auth.constantTimeEquals, "function");
assert.equal(Auth.constantTimeEquals("same", "same"), true);
assert.equal(Auth.constantTimeEquals("same", "diff"), false);
assert.equal(typeof Config.boolean, "function");
assert.equal(typeof Config.required, "function");
assert.equal(typeof Config.requiredSecret, "function");
assert.equal(typeof Health.createRegistry, "function");
assert.equal(typeof Metrics.createRegistry, "function");
assert.equal(typeof Realtime.channel, "function");
assert.equal(typeof Realtime.event, "function");
assert.equal(typeof Realtime.backplane.memory, "function");
assert.equal(typeof SloppyRealtimeError, "function");
assert.equal(typeof RateLimit.fixedWindow, "function");
assert.equal(typeof RateLimit.slidingWindow, "function");
assert.equal(typeof RateLimit.tokenBucket, "function");
assert.equal(typeof RateLimit.concurrency, "function");
assert.equal(typeof RateLimit.memory, "function");
assert.equal(typeof SloppyRateLimitError, "function");
assert.equal(typeof TestHost.create, "function");
assert.equal(typeof TestHost.fromArtifacts, "function");
assert.equal(typeof TestHost.fromPackage, "function");
assert.equal(typeof TestServices.postgres, "function");
assert.equal(typeof TestServices.sqlServer, "function");
assert.equal(RootWebhooks, Webhooks);
assert.equal(RootTestWebhooks, TestWebhooks);
assert.equal(typeof Webhooks.event, "function");
assert.equal(typeof Webhooks.outbox, "function");
assert.equal(typeof Webhooks.verify, "function");
assert.equal(typeof TestServices.docker.available, "function");
assert.equal(typeof FakeClock.fixed, "function");
assert.equal(typeof TestData.sqliteMemory, "function");
assert.equal(RootBackgroundService, BackgroundService);
assert.equal(RootFfiTypes, t);
assert.equal(RootUnsafeFfi, unsafeFfi);
assert.equal(RootWorkQueue, WorkQueue);
assert.equal(RootOrm, orm);
assert.equal(RootTable, table);
assert.equal(RootColumn, column);
assert.equal(RootRelation, relation);
assert.equal(RootWorkerPool, WorkerPool);
assert.equal(RootWorker, Worker);
