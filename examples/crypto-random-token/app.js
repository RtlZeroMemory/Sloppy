import { Random } from "sloppy/crypto";

const resetToken = Random.token(32);
const recoveryCode = Random.numericCode(6);

export default {
    requestId: Random.uuid(),
    randomBytesLength: Random.bytes(32).byteLength,
    resetTokenLength: resetToken.length,
    randomHexLength: Random.hex(32).length,
    recoveryCodeLength: recoveryCode.length,
};
