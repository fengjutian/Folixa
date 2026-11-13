# AI Worker IPC protocol

## Transport

The implementation uses a local named pipe through Qt Local IPC. Messages are UTF-8 JSON objects delimited by a single newline. The maximum accepted frame size is 1 MiB.

**Protocol version: 3.** Version 2 added the `event` frame type for streaming output.
Version 3 adds the `context` payload to `ai.chat.stream` and the `qa` task type (whole-document
question answering with citations). A client that only understands an earlier version cannot
handle version 3 frames — the main program and the Worker must always ship together.

## Request

```json
{
  "protocol_version": 3,
  "request_id": "uuid",
  "operation": "system.health",
  "payload": {}
}
```

## Response

```json
{
  "protocol_version": 3,
  "request_id": "uuid",
  "type": "result",
  "ok": true,
  "payload": {
    "status": "ready"
  }
}
```

Errors use `type: "error"`, `ok: false`, and a stable error object containing `code` and `message`.

## Event (protocol 2, unchanged in version 3)

An event is an **intermediate** message for a request that is still running. The Worker sends
many events for one request id, then a final `result` or `error`.

```json
{
  "protocol_version": 3,
  "request_id": "uuid",
  "type": "event",
  "event": "ai.stream.delta",
  "payload": { "text": "增量文本" }
}
```

Two rules matter:

- **Event frames deliberately have no `ok` field.** `ok` belongs to final results only.
  A client that mistakes an intermediate event for a finished request should be visible at a glance.
- **An event does not finish the request.** On `type == "event"` the client must not stop the
  pending timeout timer and must not clear the streaming state, otherwise the UI reports
  “done” before the answer is actually finished.

Every request ends with exactly one final `result` or `error` frame — including a streamed
request, which emits its events first and then a final result with `status: "finished"`.
The client clears its streaming state on that frame; a Worker that only sends events would
leave the request marked as running forever.

## Operations

| Operation | Purpose |
|---|---|
| `system.health` | Verify protocol and Worker readiness |
| `system.shutdown` | Ask the Worker to acknowledge and exit |
| `ai.echo` | Development-only streaming-path placeholder |
| `ai.provider.status` | Report the active Provider: id, model, demo flag, prices |
| `ai.chat.stream` | Start a streaming chat; replies with `status: "streaming"` then events |
| `ai.chat.cancel` | Cancel the in-flight stream identified by `payload.target` |

`system.health` returns `status`, `protocol_version` and `worker_version` in the payload.
`system.shutdown` returns `status: "stopping"` and the Worker exits after the frame is flushed.

Unknown operations return `operation_not_supported`. Protocol version mismatches return `protocol_version_unsupported`.

### `ai.chat.stream`

Payload: `task` (`summary` / `explain` / `translate` / `chat` / `qa`), `messages` (the fully built
prompt), and `data_scope` (the user-visible description of what is being sent, e.g.
“第 3 页 · 1,240 字”).

For the `qa` task the payload also carries `context`: the retrieved, numbered context blocks.

```json
{
  "protocol_version": 3,
  "request_id": "uuid",
  "operation": "ai.chat.stream",
  "payload": {
    "task": "qa",
    "data_scope": "整篇文档 · 检索到 2 块 · 320 字",
    "messages": [ { "role": "system", "content": "…" }, { "role": "user", "content": "…" } ],
    "context": [
      { "number": 1, "page": 1, "text": "…", "chunk_id": "a1b2b0" }
    ]
  }
}
```

`number` is the citation number the model refers to with `[n]`; `page` is 1-based.
The structured copy exists so a Provider can cite **real** blocks (the demo Provider quotes
the first line of a block verbatim) instead of inventing plausible-looking text. The main
program verifies every citation against these blocks after the answer arrives.

The Worker answers with a `result` frame carrying `status: "streaming"` and then emits
events for the same request id:

| Event | Payload | Meaning |
|---|---|---|
| `ai.stream.delta` | `text` | One increment of the answer |
| `ai.stream.done` | `finish_reason`, `input_tokens`, `output_tokens`, `estimated_cost` | Stream finished |
| `ai.stream.error` | `code`, `message` | The request failed (error code is mapped to a Chinese hint) |
| `ai.stream.cancelled` | `cancelled` | The request was cancelled |

After the terminal event the Worker sends a final result frame with `status: "finished"`.

### Error frames

An error frame (`type: "error"`, `ok: false`) is also a failed request: the client reports it
through the same failure path as a timeout or a lost connection, so the UI always shows a
reason. The `message` in an error frame is user-facing text and is written in Chinese.

### `ai.chat.cancel`

Payload: `target` (the request id to cancel). The Worker replies `status: "cancelling"`
and later emits `ai.stream.cancelled` for that request id.

## Security requirements

- The server name contains an application-specific random session identifier.
- The Worker accepts only a single owning client.
- Requests are bounded by frame size and parsed as complete JSON objects.
- The Worker receives no arbitrary file, shell, process or network tool.
- **API keys never cross the wire.** The key is read from the Windows Credential Manager
  inside the Worker process and handed straight to the Provider. No frame, log line or
  trace record ever contains it.
- The Worker performs no automatic redirect following: a redirect would carry the
  `Authorization` header to a different host.
- Future document operations must carry document ID, revision and a scoped capability token.
