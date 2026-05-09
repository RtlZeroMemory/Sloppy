# Time Basic Example

This example documents the intended `sloppy/time`
delay and timeout shape; execution remains lane-specific like the other app-host examples.

```js
import { Time } from "sloppy/time";

await Time.delay(250);
const summary = await Time.timeout((signal) => loadUserSummary({ signal }), { afterMs: 1000 });
```

The Time API is async-first and Promise-friendly.

## Limitations

This example is limited to delay and timeout behavior. Cron-style scheduling is
covered separately.
