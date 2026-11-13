# Server model: Discord-style spaces, channels and permissions

Status: **decided, not started**. The owner approved D1 and D3 on 2026-11-12 in
the revised form below. No server or client code has been written for this yet.

This document replaces the server assumptions in
`2026-10-26_22-31_screen-share-context.md` (sections 4 and 5). It does not
change the voice rule: voice must never be interrupted by screen sharing.

---

## 1. What the owner decided

- **D1 (revised).** The server model changes completely. A server is a
  Discord-style space that holds any number of **text channels** and **voice
  channels**. The current one-voice-channel setup is not the target. Development
  that keeps it would be a step backward, so it is not an option.
- **D3 (revised).** Permissions follow the Discord model: a server-wide
  permission layer, plus overrides on each **voice channel** and each **text
  channel**.
- **Media server (D1 SFU).** The LiveKit recommendation in the earlier context
  doc still stands as a candidate. The choice of SFU has not been re-approved
  for the new model. Each channel that shares a screen needs its own media room,
  so the SFU decision must be made again against this model (section 5).

## 2. What Murmur already has (verified in the repository, 2026-11-12)

The starting point is better than "one voice channel". Murmur is built on a
channel tree with permissions:

- **Channel tree.** `addChannel(name, parent)` and `removeChannel` exist in the
  Ice interface (`src/murmur/MumbleServer.ice:664`, `:671`). Channels nest and
  have parents and links (`src/murmur/DBWrapper.cpp:552`, `:709`).
- **Per-channel permissions.** `Server::hasPermission(ServerUser*, Channel*,
  perm)` (`src/murmur/Server.h:352`) checks `ChanACL` entries per channel.
  Permission checks are exposed over Ice as `hasPermission`
  (`MumbleServer.ice:620`).
- **Groups and ACLs in the database.** `ACLTable`, `GroupTable`,
  `ChannelPropertyTable`, `ChannelListenerTable` (`src/murmur/database/`).
- **Text messages.** Murmur delivers text messages to channels and users.
  **Not yet verified:** whether the server stores text history. Check before
  designing the text channels.

So the gap is mainly in what the server exposes and what the client shows, not
in the basic model. A "one voice channel" setup on this server could be a
configuration or client UI limit. **Open item O1** below asks the owner to
confirm this before any rewrite.

## 3. Target model

| Discord concept | Murmur today | Target |
|---|---|---|
| Server | Murmur instance | Same. One Murmur instance is one server (space). |
| Category | Channel with children | Channel that groups text and voice children. |
| Voice channel | Murmur channel with voice | Same, with its own permission overrides. |
| Text channel | Murmur channel text chat | Named text channel with its own permission overrides. |
| Role | Group in ACL | Named role, mapped onto Murmur groups (see O2). |
| Channel override | ChanACL on a channel | Allow/deny per role or member, per channel. |
| Screen share | not present | Per voice channel, through the sidecar (D1 media room per channel). |

## 4. Permission model (D3)

- Layers, in order: server default, role, channel override, member override.
  A deny at a more specific layer beats an allow at a broader one.
- Every check runs on the server. The client only hides or shows controls.
- Screen-share rights are a permission on the voice channel (for example
  `Share screen`). They are checked by the server before any token is issued.
- Token minting (the server change in the earlier D3) signs short-lived tokens
  only for a channel the user can access and share in. Clients never hold the
  signing key.
- **Known gap today:** Murmur's plugin-message relay does not check permissions
  (`src/murmur/Messages.cpp:2546`). Any screen-share signalling must go through a
  check that the current server code does not do.

## 5. Phases (new order)

Each phase is a separate change with its own commit. Each one must compile and
is verified by a test or a manual run before the next starts.

1. **Confirm the baseline.** Answer O1. Run a Murmur with several voice and
   text channels and record what the client shows. This is the proof of the
   "one channel" claim.
2. **Server model.** Expose the channel tree and text channels in the client
   and Ice API as needed. Add the permission layers from section 4 where Murmur
   lacks them. Tests: permission checks for each layer.
3. **Roles and overrides.** Map roles onto groups (O2). Tests: deny beats
   allow across layers.
4. **Screen-share signalling.** Per-channel share state, permission checks on
   every share message, and token minting (depends on O3 and on the SFU choice).
5. **Media.** SFU integration per channel (depends on D1 being re-approved for
   this model).
6. **Network QoS and installer.** As in the earlier phase list.

The earlier phases 3 and 4 map onto steps 4 and 5 here.

## 6. Open items and owner answers

- **O1. Answered: not seen in the client.** The client shows "Add channel" only
  when the user has `Write` or `MakeChannel` permission on the parent channel
  (`src/mumble/MainWindow.cpp:2697`). The likely cause is that the test user is
  not an admin, so the option is hidden. **To confirm:** log in as the server
  admin and check for the option. If it is there, the limit is in permissions,
  not the server.
- **O2. Answered: the owner will look at existing Discord-style CSS or themes.**
  Open point: the Mumble client is a Qt (C++) desktop app, not HTML/CSS, so
  CSS cannot be used directly. Discord's own CSS, assets and code are its
  property and must not be copied. The practical route is our own Qt theme that
  copies the layout and feel, and not Discord's files. Roles (groups or a new
  role concept) are still open.
- **O3. Answered: keep history indefinitely, with an admin option to clear older
  messages.** Admin clear needs a permission of its own. Storage and privacy
  effects are still to be sized.
- **O4. Open: LiveKit.** Explained to the owner on 2026-11-12. Screen share is a
  per-channel video stream. LiveKit is a separate media server that receives
  each sharer's screen and forwards it to the others in the same voice channel.
  Voice stays on Murmur. The owner has not yet approved LiveKit for this model.
  The C++ client SDK maintenance check is still open.
- **O5. Answered: no fixed limit.** Users can create as many text and voice
  channels as they want. A practical ceiling for performance and storage still
  needs a test later.

## 7. How to resume

1. Read this document, then `AGENTS.md`, `COMMIT_GUIDELINES.md` and
   `2026-10-26_22-31_screen-share-context.md`.
2. Answer O1 before any server code changes.
3. Keep the voice path unchanged while changing server code (see the locking
   rules in `docs/dev/MurmurLocking.md`, which apply to `Server` and
   `ServerUser` work).
4. Never add WebRTC or capture headers to `src/mumble/` (rule 4 of the earlier
   document).
