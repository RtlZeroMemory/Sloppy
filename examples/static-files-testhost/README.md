# Static Files TestHost

Shows static files through `TestHost.create(app)`. This mode exercises the
in-process path safety, headers, precompressed selection, `HEAD`, and route
precedence behavior without building artifacts.

This example is covered by the repository bootstrap TestHost checks. It is not
a `sloppy run` runtime example.
