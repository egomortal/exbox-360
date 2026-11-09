# Launch and return contract

Status: design complete; execution disabled pending owner/Claude hardware proof. This is a logical application contract, not a daemon wire protocol or implementation. No daemon internals or PS5X360 implementation files were inspected. Existing `Intent.dispatch_allowed` remains false.

## Identity and request

Keep the selected library key distinct from the launched title. Xbox 360 entries request the installed title **PPSA50011**, with no game argument, path, deep link or configuration write. All 360 tiles open the player's picker and explain that selection happens there. PS5 entries use their supplied installed title ID. The companion owner must provide the approved client interface; Games code must not invent an endpoint, opcode, payload or privilege mechanism.

Before dispatch, the future adapter requires recorded v4b reboot proof, confirmed player installation and the approved launch/return interface. Unknown proof or unavailable service leaves the dashboard active with an actionable error. Save a versioned dashboard-owned snapshot: library key, source filter, visibility settings, search query, selected control/grid focus and scroll position. Do not write into PS5X360 storage. The dashboard's own return title ID must come from its installed identity, not a guessed constant.

## Lifecycle

| State | Event | Result |
| --- | --- | --- |
| Browsing | A on tile, proof missing | Stay browsing; explain launch pending |
| Browsing | A, adapter admitted | Persist snapshot, create unique request ID, enter LaunchPending |
| LaunchPending | Repeated A | Ignore; one outstanding request |
| LaunchPending | Rejection or definite failure | Restore controls/focus, show error; explicit user retry |
| LaunchPending | Accepted | Await confirmed foreground transition; acceptance is not gameplay success |
| LaunchPending | Deadline/no acknowledgement | Mark outcome unknown; reconcile foreground before retry; never blind duplicate launch |
| LaunchPending | Confirmed target foreground | Suspended; let platform lifecycle release dashboard resources |
| Suspended | Explicit supported return request | Approved companion adapter requests dashboard title; no game-specific argument |
| Suspended | Dashboard resumes or cold-starts | Restore saved view, reconcile current entries, clear completed request |
| Any | Late or mismatched request response | Ignore for current transaction |

Transport deadlines belong to the approved adapter, not game-loading timers. A successful switch never marks the selected 360 tile played: the user can choose a different game in the picker. Do not implement continue-last or automatic retries. Dashboard restarts must reconcile pending state rather than replay it.

## Return semantics and fallback

The public guide offers return to the emulator library and exit; neither documents an automatic Ex-Box return. A clean one-button return therefore remains a required companion-owner capability to prove. No input interception, process injection, emulator modification or implicit background monitor is proposed here. Where a supported explicit return path exists, it requests the dashboard's verified installed title ID. Otherwise the owner returns via the system home screen and opens Ex-Box; dashboard state restoration must work equally on a cold start. Do not label the manual fallback seamless.

Restore selection by library key, not old index. If removed/hidden/search-filtered, select first visible entry; if empty, focus controls. Keep query/filter/hide values and clamp scroll. Missing artwork never blocks return. Failed return must leave the current title/system controls usable; do not kill/relaunch either application to simulate recovery.

## Admission evidence and gate

Owner/Claude supplies: reboot identity/date, daemon version, installed source/destination title identities, acknowledged title launch, observed foreground switch, explicit return action and observed dashboard resume/cold start. Include failure behavior for missing player, unavailable daemon, duplicate input and uncertain acknowledgement. No hardware tests are performed by this chat.

Design gate: every lifecycle event above has an outcome; no deep-launch claim; no guessed protocol or return title; no gameplay-success inference; disabled current dispatch preserved. PASS by contract review, not hardware verification.
