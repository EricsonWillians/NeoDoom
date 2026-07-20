# Batched Real-Time Updates

This example defines an invisible, non-blocking marker actor, creates 48 live
handles, and animates them with one `bd.apply_actor_batch` call every two tics.
It periodically reads `bd.profile()` rather than constructing per-actor
snapshots.

Use this pattern for dense control loops: native callback filters, compact live
handles, and one bulk crossing keep Python overhead explicit and measurable.
