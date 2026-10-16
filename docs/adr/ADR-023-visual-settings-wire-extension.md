# ADR-023: Optional authored visual settings in wire family v1

State: accepted for M04.C03; no milestone qualification follows this decision.

Crop/fit/fill belongs to surface content, layer opacity/blend belongs to reusable
composition definitions, and patch mask/opacity/blend belongs alongside physical
calibration. These settings must survive logical restoration and share the same
registry, validation, atomic commands and undo path as existing controls.

Add three typed optional members: `Layer.visual`, `Surface.content` and
`ProjectionPatch.visual`. Absence or null resolves to explicit defaults: full crop,
stretch, no mask, opacity one and premultiplied over. The decoder permits omission
only of these three newly registered members. All previous required fields and
unknown-field rejection remain strict. Settings objects themselves require all
their fields. Encoding includes the members, using null for unmaterialized defaults.
JSON Schema and generated TypeScript derive from the same field registration.

This is an additive optional extension of the existing v1 document family, which
reads earlier documents without rewriting geometry/calibration or source ownership.
Earlier engines reject expanded documents they cannot understand; they do not
silently discard settings. Incompatible structural changes still need a new
version and migration. No general missing-field or unknown-field leniency is added.

Fit/fill use the cropped chart region's pixel aspect and the logical surface's
mean opposite-edge aspect, centred in surface UV before chart mapping. Crop acts
in composition UV. Masks use original surface UV, independently of crop and fit.
Rectangle/ellipse masks support normalized inward feather and inversion. Opacity
scales all premultiplied components. Over/add/screen use linear RGB with over alpha;
add can accumulate brighter intermediates. SDR foreground RGB clips to 0..1 when
consumed, and declared named/display encodings clip at export. No HDR output or
unsupported multiply mode is claimed. Native fixed-function blending preserves
transparent composition targets and opaque physical-output targets.

Portable tests cover earlier-document defaults, unknown/required field rejection,
atomic invalid edits, undo, persistence, chart-aware fit, mask coverage and blend
equations. GPU qualification adds six visual phases over three windows, with the
existing fixed 2/255 tolerance. Engine qualification checks registry and granted
OSC application without changing source instances, placements or calibration.
