# ADR-019: project registry as the OSC/OSCQuery authority

State: accepted for M03.C02 adapter implementation. This is not transport or
interoperability qualification; those gates need actual served/client evidence.

The application already owns typed validation, immutable authored state,
transactions, revision checks, reliable sessions and structural commands. OSC
and OSCQuery must enter that same model and expose its complete inventory.

libossia was reviewed at `d606b8801eb4315f84dea6142ead29ed1d1fc4b6`, including its
[generic parameter setter](https://github.com/ossia/libossia/blob/d606b8801eb4315f84dea6142ead29ed1d1fc4b6/src/ossia/network/generic/generic_parameter.cpp),
[OSC receive path](https://github.com/ossia/libossia/blob/d606b8801eb4315f84dea6142ead29ed1d1fc4b6/src/ossia/network/osc/detail/osc_receive.hpp)
and [OSCQuery server](https://github.com/ossia/libossia/blob/d606b8801eb4315f84dea6142ead29ed1d1fc4b6/src/ossia/network/oscquery/oscquery_server.cpp).
The generic setter updates its stored value before notifying callbacks. A custom
parameter bridge could interpose application validation; the generic mirror alone
would not establish the project's atomic rollback, exact rational/uint64 or
structural/reliable-command contracts. This is a conclusion about that integration
path, not a claim that libossia cannot support a custom implementation. Its reviewed
LICENSE contains LGPL-3.0 terms; licensing is not the reason for this choice.

Use a small authored OSC codec and an immutable OSCQuery projection generated
from ParameterRegistry. Converted writes remain typed operations for the existing
validator. Keep exact integer/rational values, access, units, ranges, persistence,
defaults and capability metadata. Do not publish a second writable state tree.
The reviewed proposal baseline is
[`e3d8e3b91020c617f47fd88de157a1fe053e0583`](https://github.com/Vidvox/OSCQueryProposal/tree/e3d8e3b91020c617f47fd88de157a1fe053e0583),
with the [OSC 1.0 wire specification](https://opensoundcontrol.stanford.edu/spec-1_0.html).
No third-party implementation code is copied into the new codec/projection.

Network transport should use an established HTTP/WebSocket implementation rather
than another custom framing stack. Boost.Beast/Asio is the selected next transport
candidate: the pinned vcpkg registry provides 1.92.0 under BSL-1.0. It is not yet a
linked dependency in this first portable slice. Review and retain every resolved
package/source/license when adding it; do not infer an installed runtime or
successful network service from this decision.

Required validation includes independent OSC byte fixtures; complete registry/tree
inventory and addressing; strict type/access/range checks; exact-path LISTEN/IGNORE
and binary WebSocket values; structure invalidation; reconnect; bounded parsing,
queues and slow clients; actual HTTP/UDP/WebSocket interoperability. Advertise only
implemented extensions. An in-process tree projection is not a complete OSCQuery
server. Non-immediate timetags remain explicitly unavailable until native show
scheduling exists, and UDP does not gain reliable acknowledgement/deduplication
semantics merely by using OSC bundles.
