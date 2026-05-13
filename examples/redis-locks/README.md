# Redis Locks

This is an API-shape example for `Redis.locks(...)`.

It shows a single-key Redis lease with bounded acquisition, explicit TTL, lease
extension, and release through async disposal.

Current limitations:

- requires Redis and the Sloppy outbound network bridge when executed;
- locks are single Redis-key leases, not Redlock or multi-node consensus;
- owner tokens are internal and redacted;
- cluster and sentinel behavior are not claimed.
