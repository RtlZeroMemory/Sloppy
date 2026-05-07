# Ryu

This directory vendors the C shortest float-to-string conversion subset from
`https://github.com/ulfjack/ryu`.

Slop uses Ryu only behind canonical `sl_string_format_*` helpers. Slop-owned code should not
call Ryu entry points directly.

The vendored files are dual-licensed under Apache 2.0 or Boost 1.0; see
`LICENSE-Apache2` and `LICENSE-Boost` in this directory.
