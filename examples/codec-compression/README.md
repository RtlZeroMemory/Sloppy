# Codec Compression Example

This example documents zlib-backed gzip/gunzip
helpers and the explicit decompression output limit.

```js
import { Compression } from "sloppy/codec";

const gz = await Compression.gzip(bytes, { level: 6 });
const raw = await Compression.gunzip(gz, { maxOutputBytes: 1024 * 1024 });
```

Compression uses vetted backend libraries only.
