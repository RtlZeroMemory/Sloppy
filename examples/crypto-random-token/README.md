# Crypto Random Token Example

Status: CORE-CRYPTO-01.I source example. This example documents the intended
`sloppy/crypto` random helper shape for UUIDs, random bytes, URL-safe tokens, hex text, and
numeric codes.

```js
import { Random } from "sloppy/crypto";

const resetToken = Random.token(32);
const recoveryCode = Random.numericCode(6);
```

Random helpers use OS CSPRNG backends only. The example does not print generated tokens or
claim randomness quality from deterministic tests. It has no Math.random fallback,
no package-manager behavior, no WebCrypto/Node/Bun compatibility promise, no public alpha
claim, and no benchmark claim.
