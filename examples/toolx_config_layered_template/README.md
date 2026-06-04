## toolx-config Layered Template

This starter shows a realistic two-file workflow for `toolx-config`.

- `app.base.json`: checked-in defaults
- `app.local.json`: machine- or environment-specific overrides

Suggested flow:

```bash
toolx-config merge --base app.base.json --overlay app.local.json --out merged.json --json
toolx-config doctor --file merged.json --require svc.host --expect svc.port=int --range svc.port=1:65535
```

Use `toolx-config snapshot-export` before local experiments if you want a quick
rollback point.
