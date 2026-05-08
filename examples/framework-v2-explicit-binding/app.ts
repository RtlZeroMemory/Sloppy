import {
    Sloppy,
    Results,
    Body,
    Header,
    Query,
    RequestContext,
    Route,
} from "sloppy";

type UserPatch = {
    displayName?: string;
    tags?: string[];
};

const app = Sloppy.create();

app.patch("/tenants/{tenantId}/users/{id:int}", (
    tenantId: Route<string>,
    id: Route<number>,
    traceId: Header<"x-trace-id">,
    includeAudit: Query<boolean>,
    patch: Body<UserPatch>,
    ctx: RequestContext,
) => Results.ok({
    tenantId,
    id,
    traceId,
    includeAudit,
    patch,
    method: ctx.request.method,
})).withName("Users.Patch");

export default app;
