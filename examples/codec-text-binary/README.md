# Codec Text And Binary Example

Status: source example. This example documents UTF-8 text helpers and the
bounds-checked Binary reader/writer surface.

```js
import { Binary, Text } from "sloppy/codec";

const payload = Text.utf8.encode("hello");
writer.u32le(1);
writer.u16be(payload.length);
writer.bytes(payload);
```

Binary methods are endian-explicit and preserve embedded NUL bytes. UTF-8 malformed input
behavior is explicit through fatal or replacement modes. This example has no Node Buffer,
Web Streams, Bun, or Deno compatibility promise, no package-manager behavior, and no benchmark
claim.
