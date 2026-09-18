# Navigation preparation attempt 2 — not passed

Source editor PID 11160 was started by this migration with `EditorSession-04.log`.
At 2026-09-18 04:59:11 UTC preparation entered `SettleGround` for CommanderSoldier;
at 05:02:04 it moved to Default, but never returned a bake/validation result.

Repeated samples around 05:32–05:42 showed worker CPU totals remaining at
488.09375 / 410.1875 seconds while the main thread alone continued accumulating
CPU time. This is a stalled completion loop, not evidence of successful baking.
The owned validation process was stopped after verifying its exact executable,
project, map and log command line. Its unsaved navigation configuration/tiles were
discarded; no user map edits were present before this attempt. Saved resource-map
migration and independent Demo_Map review work are unaffected.

Next attempt cancels obsolete queued tiles explicitly before the source-signature
check and fresh rebuild. This report and the full log are retained as failure evidence.
