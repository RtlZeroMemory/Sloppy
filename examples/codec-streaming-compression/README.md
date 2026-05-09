# Codec Streaming Compression Example

This example documents the async-iterable
compression transform shape with Time deadline/cancellation options.

```js
import { Compression } from "sloppy/codec";
import { Deadline } from "sloppy/time";

const chunks = [new Uint8Array([1, 2, 3])];
const controller = new AbortController();
const signal = controller.signal;
const stream = Compression.gzipStream(chunks, { signal, deadline: Deadline.after(1000) });
```

The stream helpers are Sloppy async-iterable transforms, not Web Streams. They keep bounded
buffering and terminal-state behavior explicit. This example has no Node, Bun, or Deno
compatibility promise, no package-manager behavior, and no benchmark claim.
