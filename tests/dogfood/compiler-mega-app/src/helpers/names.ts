import { isUserName, normalizeName } from "validator-lite";

export function toUserName(value) {
  const name = normalizeName(value);
  return { name, valid: isUserName(name) };
}
