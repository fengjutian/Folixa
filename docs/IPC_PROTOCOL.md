# AI Worker IPC protocol

## Transport

The M0 implementation uses a local named pipe through Qt Local IPC. Messages are UTF-8 JSON objects delimited by a single newline. The maximum accepted frame size is 1 MiB.

## Request

```json
{
  "protocol_version": 1,
  "request_id": "uuid",
  "operation": "system.health",
  "payload": {}
}
```

## Response

```json
{
  "protocol_version": 1,
  "request_id": "uuid",
  "type": "result",
  "ok": true,
  "payload": {
    "status": "ready"
  }
}
```

Errors use `type: "error"`, `ok: false`, and a stable error object containing `code` and `message`.

## M0 operations

| Operation | Purpose |
|---|---|
| `system.health` | Verify protocol and Worker readiness |
| `ai.echo` | Development-only streaming-path placeholder |

Unknown operations return `operation_not_supported`. Protocol version mismatches return `protocol_version_unsupported`.

## Security requirements

- The server name contains an application-specific random session identifier.
- The Worker accepts only a single owning client in M0.
- Requests are bounded by frame size and parsed as complete JSON objects.
- The Worker receives no arbitrary file, shell, process or network tool.
- Future document operations must carry document ID, revision and a scoped capability token.

