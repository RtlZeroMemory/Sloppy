import assert from "node:assert/strict";

import { TestServices } from "../../stdlib/sloppy/testing.js";

{
    const service = await TestServices.postgres({ envName: "SLOPPY_TESTSERVICES_MISSING_POSTGRES" });
    assert.equal(service.kind, "postgres");
    assert.equal(service.available, false);
    assert.equal(service.status, "SKIPPED");
    assert.match(service.reason, /SLOPPY_TESTSERVICES_MISSING_POSTGRES is not set/u);
    assert.throws(() => service.provider(), /unavailable/u);
    assert.deepEqual(service.env(), {});
    await service.close();
}

{
    const service = await TestServices.postgres({ connectionString: "postgres://user:password@localhost:5432/sloppy" });
    assert.equal(service.kind, "postgres");
    assert.equal(service.available, false);
    assert.equal(service.status, "SKIPPED");
    assert.match(service.reason, /native stdlib bridge is unavailable/u);
    assert.throws(() => service.provider(), /unavailable/u);
    assert.throws(() => service.open(), /unavailable/u);
    assert.deepEqual(service.env(), {});
    await service.close();
}

{
    const service = await TestServices.sqlserver({ envName: "SLOPPY_TESTSERVICES_MISSING_SQLSERVER" });
    assert.equal(service.kind, "sqlserver");
    assert.equal(service.available, false);
    assert.equal(service.status, "SKIPPED");
    assert.match(service.reason, /SLOPPY_TESTSERVICES_MISSING_SQLSERVER is not set/u);
    assert.throws(() => service.open(), /unavailable/u);
    assert.deepEqual(service.env(), {});
    await service.close();
}

{
    const service = await TestServices.sqlserver({ connectionString: "Driver={ODBC Driver 18 for SQL Server};Server=localhost;Database=sloppy;Uid=sa;Pwd=pass;" });
    assert.equal(service.kind, "sqlserver");
    assert.equal(service.available, false);
    assert.equal(service.status, "SKIPPED");
    assert.match(service.reason, /native stdlib bridge is unavailable/u);
    assert.throws(() => service.provider(), /unavailable/u);
    assert.throws(() => service.open(), /unavailable/u);
    assert.deepEqual(service.env(), {});
    await service.close();
}
