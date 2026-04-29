const Results = Object.freeze({
  text(body, options) {
    void options;
    return String(body);
  },
  json(value, options) {
    void options;
    const encoded = JSON.stringify(value);
    return encoded === undefined ? String(value) : encoded;
  },
});

globalThis.__sloppy_handler_1 = () => Results.json({ ok: true });
