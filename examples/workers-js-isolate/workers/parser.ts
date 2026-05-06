export async function parse(payload) {
  if (payload === null || typeof payload !== "object" || Array.isArray(payload)) {
    throw new TypeError("parse payload must be an object.");
  }
  if (typeof payload.text !== "string") {
    throw new TypeError("parse payload text must be a string.");
  }
  return {
    tokens: payload.text.split(/\s+/u).filter(Boolean).length,
  };
}
