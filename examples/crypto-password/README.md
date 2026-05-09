# Crypto Password Example

This example documents the async password
hashing, verification, and rehash policy shape.

```js
import { Password } from "sloppy/crypto";

const encodedHash = await Password.hash(password);
const ok = await Password.verify(password, encodedHash);
const shouldUpgrade = await Password.needsRehash(encodedHash);
```

Password hashing is async in the public API and offloaded from the V8 owner thread in the
V8 lane. Password values must not be logged, printed, placed in diagnostics, or committed to
goldens.

## Limitations

This example uses Sloppy's password API as provided. It does not define a custom
password hashing algorithm.
