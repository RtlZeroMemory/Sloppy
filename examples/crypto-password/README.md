# Crypto Password Example

Status: source example. This example documents the async password
hashing, verification, and rehash policy shape.

```js
import { Password } from "sloppy/crypto";

const encodedHash = await Password.hash(password);
const ok = await Password.verify(password, encodedHash);
const shouldUpgrade = await Password.needsRehash(encodedHash);
```

Password hashing is async in the public API and offloaded from the V8 owner thread in the
V8 lane. Password values must not be logged, printed, placed in diagnostics, or committed to
goldens. This example has no synchronous password hashing API,
no custom password hashing algorithm, no WebCrypto/Node/Bun compatibility promise,
no package-manager behavior, no public alpha claim, and no benchmark claim.
