# Codec Base64 And Hex Example

Status: source example. This example documents the `sloppy/codec`
Base64, Base64Url, and Hex byte transform shape.

```js
import { Base64, Base64Url, Hex } from "sloppy/codec";

const encoded = Base64.encode(bytes);
const urlSafe = Base64Url.encode(bytes, { padding: false });
const hex = Hex.encode(bytes);
```

These helpers preserve arbitrary bytes and embedded NUL values. They have no Node Buffer,
Web Streams, Bun, or Deno compatibility promise, no package-manager behavior, no public
alpha claim, and no benchmark claim.
