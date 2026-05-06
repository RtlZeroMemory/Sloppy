# Crypto Secret And Constant-Time Example

Status: CORE-CRYPTO-01.I source example. This example documents `Secret` ownership,
cleanup-once disposal, HMAC verification, and constant-time byte comparison.

```js
import { ConstantTime, Hmac, Secret } from "sloppy/crypto";

const key = Secret.fromUtf8(configuredKeyText);
try {
    const expected = await Hmac.sha256(key, body);
    return ConstantTime.equals(expected, providedSignature);
} finally {
    key.dispose();
}
```

`Secret.dispose()` is best-effort cleanup of Sloppy-owned buffers, not a guarantee that
prior JavaScript string copies, engine internals, operating-system paging, or crash dumps
are erased. The example does not print secret material and
does not claim timing-proof behavior from deterministic tests. It has
no package-manager behavior, no public alpha claim, and no benchmark claim.
