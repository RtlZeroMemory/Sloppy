import { Sloppy, Results, Route, Service } from "sloppy";

type GreetingService = {
    prefix: string;
};

type RequestCounter = {
    value: number;
};

type TransientStamp = {
    value: string;
};

const app = Sloppy.create();

app.services.addSingleton("GreetingService", () => ({
    prefix: "user",
}));

app.services.addScoped("RequestCounter", () => ({
    value: 7,
}));

app.services.addTransient("TransientStamp", () => ({
    value: "transient",
}));

app.get("/di/{id:int}", (
    id: Route<number>,
    greeting: Service<GreetingService>,
    counter: Service<RequestCounter>,
    stamp: Service<TransientStamp>,
) => Results.ok({
    message: `${greeting.prefix}-${id}`,
    counter: counter.value,
    stamp: stamp.value,
})).withName("Di.Get");

export default app;
