# Crypto Hash And HMAC Example

This example documents SHA-256 digest helpers,
incremental hashing, HMAC signing, HMAC verification, and `Secret` ownership around a
configuration-supplied key.

```js
import { Hash, Hmac, Secret } from "sloppy/crypto";

const digest = await Hash.sha256Hex(payload);
const signature = await Hmac.sha256(signingKey, payload);
const ok = await Hmac.verifySha256(signingKey, payload, signature);
```

HMAC verification uses the constant-time comparison path. The example keeps the
non-cryptographic hash namespace out of security-shaped code and does not define
a custom crypto algorithm.
