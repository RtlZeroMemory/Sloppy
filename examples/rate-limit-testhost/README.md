# Rate Limit TestHost

Use `TestHost.create(app, { clock, rateLimit: { stores } })` to isolate stores
and drive windows with a fake clock.
