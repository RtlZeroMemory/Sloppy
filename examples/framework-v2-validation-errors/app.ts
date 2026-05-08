import { Sloppy, Results, Body } from "sloppy";

type UserCreate = {
    name: string;
    email: string;
};

const app = Sloppy.create();

app.post("/users", (
    input: Body<UserCreate>,
) => Results.created("/users/1", {
    user: input,
})).withName("Users.Create");

export default app;
