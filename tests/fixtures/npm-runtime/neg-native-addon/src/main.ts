// Importing a native-addon package must fail at compile time.
import _ from "native-addon-pkg";

export async function main() {
    void _;
    return 0;
}
