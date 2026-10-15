# Windows local control transport

Run `mapgyver-engine --serve --pipe-name <stable-id>` to start the authoritative
control service with a default logical project. The endpoint is
`\\.\pipe\MapGyver.<stable-id>`. The name is a restricted ASCII ID; Windows pipe
names are case insensitive. Other platforms return an explicit unavailable exit
for this transport. This mode does not initialize a renderer or durable storage.

The engine emits one `control_ready` JSON line with endpoint, actual build identity
and configured capacities. Archive source identity remains null when unknown.
The existing diagnostic schema/feature flags keep their GPU/media/UI meanings.
Current local control availability is described by the serve handshake/capabilities.

The pipe is duplex byte mode with overlapped IO. Each UTF-8 JSON document has a
four-byte unsigned little-endian byte length followed by exactly that many bytes.
Zero or over-budget lengths close the connection before body allocation. The
initial document is the M03 handshake; later documents are reliable request
envelopes. Replies have type hello, hello_error or reply; subscription pushes have
type notification and an events payload. [Core contract](../../libs/protocol/README.md).

Sixteen peers are preallocated. Request/response JSON limits are 1 MiB/4 MiB.
There is one bounded receive frame and one bounded write frame per peer. Pending
responses prevent another request from executing; notifications are obtained from
the bounded service journal when the output slot is free. A partial frame or
incomplete handshake has a five-second absolute deadline; an entire response has
a two-second write deadline. An established idle header read has no UI-activity
timeout. A stalled peer is disconnected while other peers continue. No pipe flush
waits for an unresponsive reader. The control/IO loop is not a render deadline path.

The first instance claims the name. A protected DACL grants only the current token
user access, handles are non-inheritable, and PIPE_REJECT_REMOTE_CLIENTS is set.
These are local-user controls; IDs are not authentication credentials. Tests read
the actual DACL through GetSecurityInfo and verify its sole allow ACE against the
current token SID. Remote SMB connections have not been independently probed.
Microsoft documents [pipe access and creation flags](https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-createnamedpipew).

Reader/writer OVERLAPPED records and buffers stay owned until native completion.
Cancellation requests do not release that ownership. Closing peers are drained
asynchronously before recycling; completion events and GetOverlappedResult verify
completion. An unexpected cancellation failure/deadline is fatal to this engine
process, with a diagnostic, rather than freeing memory still owned by kernel IO.
See Microsoft's [CancelIoEx lifetime contract](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex).
Normal console Ctrl+C/Break asks the loop to stop and drain owned peers. The
integration test's service restart is an explicitly owned forced process stop;
it does not qualify graceful system shutdown or durable recovery.

The transport advances its send cursor after kernel write completion. That does
not prove application receipt. A reconnect uses the client's last actually
received event cursor and explicitly resubscribes; old replies replay from the
service cache. Subscriptions are not implicitly restored by replaying an earlier
subscribe request. Native applied revision/frame and saved revision remain null
with unavailable reasons until the corresponding workers are implemented.

The independent Python SDK fixture tests actual separate-process handshakes,
fragmented frames, DACL readback, lost acknowledgements, source/model validation,
readonly/edit ownership, notifications, partial input deadlines, native output
buffer stalls, cancelled large reply replay, handle-count stability and process
restart. It also distinguishes first-instance collision with spare capacity
(Win32=5) from a full pool (observed Win32=231), using a separate owned SDK probe.
Neither error may create a ready server or disturb the existing engine.
