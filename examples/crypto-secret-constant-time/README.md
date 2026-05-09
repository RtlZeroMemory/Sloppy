# Crypto Secret And Constant-Time Example

This example documents `Secret` ownership,
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

`Secret.dispose()` is best-effort cleanup of Sloppy-owned buffers. Prior JavaScript string
copies, engine internals, OS paging, or crash dumps may still contain data. The example
avoids printing secret material. Deterministic tests verify API behavior, not side-channel
resistance.
