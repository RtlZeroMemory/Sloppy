import { head } from "self-ref-pkg";
import { tail } from "self-ref-pkg/feature";

export async function main() {
    console.log(head + tail);
    return 0;
}
