export async function parse(payload) {
  return {
    tokens: String(payload.text).split(/\s+/u).filter(Boolean).length,
  };
}
