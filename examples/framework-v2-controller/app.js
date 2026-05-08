import { Sloppy, Results } from "sloppy";

class GreetingService {
    constructor() {
        this.prefix = "user";
    }
}

class UsersController {
    static inject = ["GreetingService"];

    constructor(greeting) {
        this.greeting = greeting;
    }

    get(ctx) {
        return Results.ok({
            message: `${this.greeting.prefix}-${ctx.route.id}`,
        });
    }
}

const app = Sloppy.create();

app.services.addScoped("GreetingService", () => new GreetingService());

app.mapController("/users", UsersController, (users) => {
    users.get("/{id:int}", "get").withName("Users.Get");
});

export default app;
