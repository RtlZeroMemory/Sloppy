const Results = Object.freeze({
  text(body, options) {
    void options;
    return String(body);
  },
  json(value, options) {
    void options;
    return JSON.stringify(value);
  },
});

globalThis.__sloppy_handler_1 = () => Results.json({ ok: true });
