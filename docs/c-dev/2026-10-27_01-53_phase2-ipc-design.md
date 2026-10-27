# Phase 2: IPC layer design

Implements Phase 2 of the screen-share plan (see
`2026-10-26_22-31_screen-share-context.md`, section 8). Scope is the link
between the voice client and the sidecar process. It carries no video and no
WebRTC.

## Goals

- The voice thread never waits on the pipe.
- A stalled, dead or misbehaving sidecar cannot grow memory or block voice.
- Malformed or oversized input from the sidecar cannot crash the client.
- Only the current user can connect.

## Transport

- Windows named pipe `\\.\pipe\mumble-sidecar-ipc`, byte mode, overlapped I/O.
- Created with `PIPE_REJECT_REMOTE_CLIENTS` and a security descriptor that
  grants access only to the current user's SID.
- Pipe instances: one. A second client is refused while one is connected.

## Framing

- Each frame: 4-byte little-endian length, then the payload.
- Maximum payload 4096 bytes. A larger declared length is a protocol error and
  the connection is closed. The receiver never allocates based on the length
  alone.
- Every frame begins with a 1-byte message type. Unknown types are a protocol
  error.

## Handshake

- The client sends `Hello` with the protocol version. The sidecar replies with
  its version. A mismatch closes the connection.
- Nothing else is accepted until the handshake completes.

## Threading

- One I/O thread per side owns the pipe handle. It is the only thread that
  reads or writes the pipe.
- The voice thread only enqueues. It never calls into the pipe.

## Queues: deliberate change from the earlier plan

The context document said control messages would be dropped when the ring is
full. That is wrong for commands. Dropping a "stop sharing" message would leave
the sidecar capturing with nothing to stop it.

Two mechanisms instead:

1. **Desired state (client to sidecar).** The client states what it wants:
   `share on <room token>` or `share off`. This is a single slot that is
   overwritten, never queued. The newest value always wins and nothing grows.
   The I/O thread sends the slot when it changes.
2. **Events (sidecar to client).** A bounded queue of fixed-size records.
   When full, the oldest event is dropped and a drop counter is incremented.
   Events are informational (state changes, errors), so losing one is safe.

Both are preallocated. No heap allocation on the voice path.

## Failure handling

- Sidecar death is detected by pipe EOF or by the process handle signalling.
- The client marks the share as stopped, keeps voice running, and restarts the
  sidecar with capped backoff.
- Invalid input from the sidecar closes the pipe and is counted; it does not
  throw across the voice path.

## Files

- `src/sidecar/IpcProtocol.h`: message types, framing constants, encode and
  decode with bounds checks. Header only, no OS calls.
- `src/sidecar/IpcBridge.h` and `IpcBridge.cpp`: the Windows pipe server and
  client, the I/O thread, the desired-state slot and the event queue.
- `src/sidecar/sidecar_main.cpp`: starts the server, runs until the client
  disconnects.

## Verification

- Framing unit tests cover: truncated frames, oversized length, unknown type,
  zero length, and exact-size boundaries.
- Queue test: overflow drops the oldest event and increments the counter.
- Manual: force-kill the sidecar during a session and confirm voice continues.
- Manual: stall the sidecar's reads and confirm the voice thread does not wait.

## Open points

- Whether the sidecar should own the pipe or the client should. The current
  plan has the sidecar as server, so it can be started and stopped
  independently of Mumble.
- Whether this change should have a GitHub issue first, per `AGENTS.md`
  (it is expected to exceed a few hundred lines).
