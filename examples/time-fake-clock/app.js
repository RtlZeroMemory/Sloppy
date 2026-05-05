import { Time } from "sloppy/time";

const clock = Time.fakeClock({ now: new Date("2026-01-01T00:00:00.000Z") });
const completed = [];

const delay = Time.delay(1000, { clock }).then(() => {
    completed.push("delay");
});

const timeout = Time.timeout(new Promise(() => {}), {
    afterMs: 500,
    clock,
}).catch((error) => {
    completed.push(error.name);
});

clock.advanceBy(500);
await timeout;

clock.advanceBy(1000);
await delay;

clock.dispose();

export default completed;
