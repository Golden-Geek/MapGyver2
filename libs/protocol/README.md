# Reliable control service core

ControlService has one control-thread owner and publishes immutable authored
state through ProjectController and ParameterRegistry. It owns no OS handle,
renderer, process or disk service. A platform transport must authenticate a
connection and bind it to the session returned by connect; request-supplied
session/client IDs cannot change that binding. IDs are identities, not credentials.

The handshake is a strict JSON object:

```json
{"protocolVersion":1,"clientId":"client-id","resumeSessionId":null,"requestEdit":true}
```

The reply supplies service/session IDs, next sequence, retention duration,
registered operation metadata and a complete snapshot. The first client
requesting edit access receives it; later clients observe. Only the owner may
transfer access to a connected target or release it. Claim/transfer/release use
an expected ownership generation, separate from authored revisions. Disconnect
releases edit ownership immediately; reconnect never displaces another owner.

Every request contains exactly these fields:

```json
{
  "protocolVersion":1,
  "serviceId":"from-hello",
  "clientId":"client-id",
  "sessionId":"from-hello",
  "requestId":"unique-request-id",
  "sequence":"0",
  "expectedRevision":"0",
  "applyAt":null,
  "operation":"transaction",
  "payload":{"commands":[]}
}
```

Sequences and revisions use canonical uint64 decimal strings. Mutating model
operations require the current authored revision. Read operations may use null;
ownership operations use their payload's expectedOwnershipGeneration. applyAt
accepts a typed MediaTime object but non-null scheduling is visibly unavailable
until a native show-clock worker is connected. No scheduling success is fabricated.
Payload fields and types are strict; transaction commands and parameter writes
use the M02 versioned wire documents. Available operations are transaction,
parameters, undo, redo, snapshot, registry, subscribe, unsubscribe, claimEdit,
transferEdit and releaseEdit. Save/native execution are unavailable, not no-ops.

Reliable identity is (service ID, session ID, sequence); requestId correlates the
outcome and must be unique within the retained cache. A session starts at sequence
zero and accepts requests in order. Valid next envelopes consume a sequence even
when domain validation, access, revision or operation checks reject them. A reply
with consumed=false means the envelope/session/sequence could not be accepted;
the next sequence has not advanced. A duplicate retained sequence must carry the
same canonical request and returns its original terminal result with replayed=true.
This includes historical read results: a replay is not a new current snapshot.

Entry/byte caps evict the oldest complete replies. The session's sequence watermark
survives eviction: expired sequences return request_expired and never execute
again. Gaps and payload changes to an old sequence reject. Terminal rejected
results are cached too. ID reuse outside the retained cache is a new sequence's
identity; clients must generate fresh correlation IDs and never turn a lost-ack
retry into a new sequence. Deduplication does not extend across a new session or
service restart. A changed service ID requires a new session and full snapshot.

Disconnected sessions retain their watermark/cache for a configurable 60 seconds
by default (positive, up to 24 hours), using a monotonic control clock. Active
sessions do not expire from UI inactivity. Retained slots are not evicted early
to admit another client. Resume must match client identity and an unconnected
retained session; subscriptions must be re-established explicitly.

subscribe carries afterEvent:null for a full snapshot, or the last received event
cursor for journal recovery. The journal has entry and batch caps. Notifications
carry authored/ownership changes and cursors; fetch a new snapshot to inspect
changed model/registry values. A journal gap returns resyncRequired with the
complete current snapshot. The transport advances its send cursor after a complete
write. Kernel write completion does not prove application receipt; clients retain
their actually received cursor across reconnects and explicitly resubscribe.
Repeated polling with the same cursor repeats a batch. No per-client unbounded
event queue is created. unsubscribe stops notifications.

Each model command stages state, serializable snapshot/registry, response cache
and journal before publication. Snapshot response capacity failure rolls the
entire edit back, so accepted state remains inspectable. Limits cover sessions,
request/response bytes, operations, cache entries/bytes, journal/batches and resume
retention. Cached payloads are immutable shared objects; accounting uses serialized
bytes, not measured allocator/RSS usage. Parser limits reject duplicate keys,
invalid UTF-8 and excessive nesting/events before typed handling.

Accepted means authored validation/publication succeeded. Replies/snapshots report
renderer and persistence unavailable with reasons, appliedRevision/showTime/frame
and savedRevision null. They never claim a render frame or durable save. M04/M12
must supply actual runtime/durable outcomes. Current tests exercise the real
portable controller/session code, including an injected control clock; these are
not IPC, native-clock, network, GPU or process-launch evidence. The Windows
[named-pipe adapter](../../adapters/windows/PIPE_CONTROL.md) now connects the real
engine with separate-process SDK tests. Generated envelope schema and
OSCQuery/HTTP/WebSocket/editor remain later M03 work.
