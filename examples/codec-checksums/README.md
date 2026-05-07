# Codec Checksums Example

Status: source example. This example documents CRC32 as a non-security
checksum helper for accidental-corruption checks and cache metadata.

```js
import { Checksums, Text } from "sloppy/codec";

const bytes = Text.utf8.encode("example");
const crc = Checksums.crc32(bytes);
```

CRC32 is not authentication, attacker-resistant integrity, a signature, HMAC, password
utility, or cryptographic hash. Use the appropriate `sloppy/crypto` primitive for
security-shaped code, including the password-hash API for passwords. This example has
no package-manager behavior, no benchmark claim, and no public alpha claim.
