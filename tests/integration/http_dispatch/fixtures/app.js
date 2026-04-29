globalThis.__sloppy_register_handler(1, function () {
  return "sloppy-ok";
});

globalThis.__sloppy_register_handler(3, function () {
  throw new Error("dispatch boom");
});
