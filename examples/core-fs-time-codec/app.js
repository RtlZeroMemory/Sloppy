import { Text } from "sloppy/codec";
import { File } from "sloppy/fs";
import { Deadline } from "sloppy/time";

const deadline = Deadline.after(500);
const bytes = await File.readBytes("data:/message.txt", { timeoutMs: 250, deadline });

export const message = Text.utf8.decode(bytes, { fatal: true });
