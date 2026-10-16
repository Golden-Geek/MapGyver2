# OSC/OSCQuery adapter contract

This portable library owns wire parsing, typed conversion, metadata projection
and subscription filtering. The separate [native transport](../../adapters/osc/NETWORK.md)
opens HTTP, UDP and WebSocket sockets on the control owner. [ADR-019](../../docs/adr/ADR-019-oscquery-adapter.md)
records the reviewed proposal/libossia sources and direct-registry choice.

OSC packets use explicit big-endian values and four-byte string/blob padding.
Supported tags are i/f/s/b and h/d/t/T/F/N/I. Strings use the declared UTF-8
extension; embedded NUL and invalid UTF-8 reject. Unknown tags (including array
designators), nonfinite numeric values, nonzero padding, truncation, trailing
bytes and capacity violations reject the complete packet. Limits cover packet
and string bytes, arguments, messages, nesting and total bundle elements.
Nested bundles retain message order and exact 64-bit NTP timetag bits. Only
immediate timetags can enter the current control ingress; scheduled/past absolute
timetags are visibly unavailable until the native show scheduler is connected.

Tree is an immutable view of ParameterRegistry. Every registered parameter and
structural action has the same canonical path, with FULL_PATH, TYPE, ACCESS,
DESCRIPTION and applicable VALUE/RANGE/UNIT metadata. X_MAPGYVER carries the
original typed model/default/range/persistence/availability/identity metadata.
There is no writable mirror state. A node may own a value and children when a
stable entity ID equals an ordinary field name such as order; neither disappears.

Mapping is explicit:

| Model value | OSC TYPE | Encoding |
| --- | --- | --- |
| Boolean | T | T/F interchangeable, JSON boolean VALUE |
| Bounded unsigned scalar with JSON-safe range | h | Signed int64 wire value, validated nonnegative/range |
| Other uint64 | s | Canonical decimal string preserves the full range |
| Real / point | d / dd | Finite IEEE double values |
| Rational / media time | ss / sss | Exact signed decimal components, validated by the model |
| Text/reference/enums | s | UTF-8 string; enum values are listed in RANGE |
| Optional reference/binding/ID list/topology/seams | s | JSON value with the model's existing schema |
| Structural command | s | JSON object containing expectedRevision and versioned command |
| Undo/redo | s | Canonical expected authored revision |

Clients send the advertised quantity/type; no implicit float/integer narrowing
is accepted. Unsupported unsigned ranges remain decimal strings rather than
rounded OSCQuery JSON numbers. Native read-only fields remain read-only even for
the editing owner. Structural payloads must match the invoked registered action.

query implements full/subtree and single-attribute projections, HOST_INFO (path
independent), known absent attributes, write-only VALUE=204, bad query=400 and
missing path=404. A URL fragment is disregarded and percent encoding is checked.
Streaming flags are false by default; the bound native transport enables them.
In-process projection alone is not a complete OSCQuery service. No Bonjour or
remote success is claimed by the portable tests.

Subscription accepts only the proposal's LISTEN/IGNORE shape and existing paths.
Its bounded set is exact: a container subscription does not cover descendants.
IGNORE is idempotent, a new connection starts empty, removed paths are pruned,
and structural changes generate root PATH_CHANGED for client reload. Values use
the standard binary OSC encoding. Actual socket queues/backpressure and client
interoperability must be validated with the transport implementation.

Ingress creates a dedicated read-only external OSC session. An existing editing
owner must explicitly transfer access to it; it cannot claim or displace another
editor. Converted values/commands use the same reliable service's validator and
authored state. Immediate all-parameter bundles and all-structural-command bundles
are atomic. Structural commands require one consistent expected revision; history
triggers are individual. Mixed parameter/structural bundles reject before mutation
and require an explicit structural transaction. Empty bundles do nothing.

Bare OSC is not the reliable client envelope: packet loss/reordering/repetition
does not acquire acknowledged retry semantics from the ingress's internal service
session. Parameter writes use the current authored revision; structural revision
preconditions reject stale repeats. Use the reliable command channel for exact
acknowledgement/deduplication. Rendered/applied and durable/saved outcomes remain
unavailable as reported by the service. The transport must close the ingress
session explicitly on shutdown, releasing only its edit ownership.
