# Codec Checksums Example

Status: CORE-CODEC-01.J source example. This example documents CRC32 as a non-security
checksum helper for accidental-corruption checks and cache metadata.

```js
import { Checksums } from "sloppy/codec";

const crc = Checksums.crc32(bytes);
```

CRC32 is not authentication, attacker-resistant integrity, a signature, HMAC, password
utility, or cryptographic hash. Use `sloppy/crypto` Hash or Hmac for security-shaped
code. This example has no benchmark claim, no package-manager behavior, and no public
alpha claim.
