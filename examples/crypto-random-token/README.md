# Crypto Random Token Example

This example documents the intended
`sloppy/crypto` random helper shape for UUIDs, random bytes, URL-safe tokens, hex text, and
numeric codes.

```js
import { Random } from "sloppy/crypto";

const resetToken = Random.token(32);
const recoveryCode = Random.numericCode(6);
```

Random helpers use OS CSPRNG backends only. The example does not print generated
tokens or describe randomness quality from deterministic tests. There is no
`Math.random` fallback.
