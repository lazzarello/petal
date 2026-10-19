# Linux build: compile-only target and original failure plan

## Compile-only target implemented

Linux now selects `apps/desktop/src-tauri/src/session_linux.rs` explicitly with
`cfg(target_os = "linux")`; Windows still selects `session_stub.rs` unchanged.
Linux session queries report no room, shares, or publishing media, and remote
control stays off. Session mutation commands reject unsupported operations.
Permission stubs do not claim grants. Windows-only startup hooks, commands,
and telepointer code/tests are gated to Windows. Shared modules remain compiled;

Verified in the repository's devenv shell, from `apps/desktop/src-tauri`, on
`x86_64-unknown-linux-gnu` (existing desktop `node_modules` used):

| Command | Exit | Result |
|---|---:|---|
| Baseline `cargo check --locked --lib` | 101 | Reproduced 75 errors; `/tmp/petal-linux-baseline.log` |
| `cargo check --locked` | 0 | Library and binary type-check |
| `cargo build --locked` | 0 | Linked `target/debug/desktop` |
| `cargo test --locked --lib linux_` | 0 | Both new Linux stub tests pass |
| `cargo test --locked --lib` | 0 | 1343 passed, 2 ignored |

Existing compiler warnings and the optional missing-CUDA warning remain. No
Linux runtime/media support is claimed. GUI startup, release packaging, other
Linux architectures, and native Windows/macOS regression builds are unverified.

## Original plan (historical)

**Original status:** planning only. Do not resume the abandoned build-to-green workflows. Their last `cargo check --locked --lib` failed with **75 errors** (log: `/tmp/petal-linux-check-lib.log` on the current machine). Neither a Linux build nor macOS/Windows regression was proven. The existing `pipewire-video` worktree has uncommitted code, devenv, and spike files; preserve them. A future session should regenerate the compiler log rather than assume `/tmp` survives.

## Reproduce the baseline in a future session

From the repository root, enter `devenv shell`, then:

```sh
cd apps/desktop
npm ci
cd src-tauri
cargo check --locked --lib
```

`build.rs` runs Vite, hence `npm ci`. Keep the **first** failed log and classify it before changing code; dependent diagnostics can disappear when a single cfg boundary is corrected. The existing Linux `libc` dependency in `Cargo.toml` resolved 14 earlier errors and is already reflected in the 75-error baseline.

## Failure inventory

| Rust diagnostic | Count | Likely cause |
|---|---:|---|
| E0433 unresolved paths | 64 | Mostly Windows-only modules referenced in Linux-selected code; includes generated Tauri command symbols and unavailable AI-chat panel. |
| E0425 missing functions/values | 7 | Mainly Windows APIs and helpers not defined on Linux. |
| E0277 unsized `str` | 2 | `telepointer.rs` around line 1557; possibly a cascade from an unresolved Windows overlay, **not yet proven**. |
| E0432 unresolved import | 1 | `session_stub.rs:25` imports Windows-only `windows_capture_target`. |
| E0061 argument count | 1 | `session_stub.rs:3082` calls `start_compositor_feed` with six arguments; the **Linux** definition in `transport/subscriber.rs:3375` takes four. |
| **Total** | **75** | By source location: `session_stub.rs` 58, `lib.rs` 12, `telepointer.rs` 5. |

**Important correction to the apparent E0061 bug:** `transport/subscriber.rs:2400` defines the **Windows** `start_compositor_feed` with **six** arguments, matching the call in `session_stub.rs`; Linux selects a different, four-argument no-op definition. This is a platform-selection/API-boundary error, *not evidence of a Windows caller bug*. Do not delete the `Room` or forced-disconnect sender just to satisfy the Linux compiler suggestion: Windows uses them for subscription and teardown. Confirm cfg-specific signatures again before editing.

## Workflow for a future coding session

1. **Map target boundaries (read-only).** Inventory every caller of `session::*` and `start_compositor_feed`. `lib.rs:196–200` currently routes **all non-macOS** targets to the Windows-oriented `session_stub.rs`. Record which session APIs can remain portable and which must honestly reject Linux capture/control. Decide whether a separate Linux session module is less invasive than gating dozens of Windows paths. **Gate:** a documented Linux session API contract with no invented permission or success states.
2. **Split independent file ownership (parallel only after step 1).** One owner takes session selection/Windows-specific session code (`session_stub.rs` plus minimal `lib.rs` module declarations). A second takes Linux-only Tauri setup and command registration in `lib.rs` (coordinate edits to that file serially). A third takes `telepointer.rs` and peripheral stubs. The session owner must retain the Windows six-argument subscriber call; Linux must never invoke a Windows capture/compositor path. The registration owner checks `ai_chat::panel`, `autofill`, `remote_control_answer_escalation`, and the three Windows-only region-share commands against their `#[cfg]` definitions. **Gate:** review a diff for changes to Windows/macOS cfg paths before integrating.
3. **Integrate serially using compiler evidence.** Re-run `cargo check --locked --lib`; regroup remaining errors by *root cause*, not by count of generated macro messages. Retest both E0277 diagnostics only after unresolved overlay names are fixed. Linux unsupported operations that return `Result` must return explicit errors, not successful no-ops; inspect permission and room-join semantics. Add a focused check for new nontrivial stub behavior. **Gate:** zero Linux library compiler errors without deleting the app's shared functionality.
4. **Validate the actual target and preserve supported platforms.** Run `cargo check --locked`, then `cargo build --locked` to expose linker/native dependencies beyond type-checking. Run relevant tests. Windows/macOS CI or native builds must check capture, subscriber disconnect, handler availability, remote-control denial/revoke, camera, and startup. **Gate:** record command, target, exit code, and any unverified platform explicitly; no claim of Linux media support from a compiling binary.

**Stop conditions:** do not widen Windows APIs to Linux, hide an entire application module solely to pass the compiler, report capture/control/permissions as successful when unsupported, or alter Windows subscriber disconnect behavior without Windows tests. If the session split grows beyond a bounded compile-only target, stop with the remaining classified diagnostics rather than piling on stubs.

The read-only classification workflow made **no source edits or builds**. Current uncommitted Linux changes and the older capture-spike result are separate work; do not discard either without reviewing the diff.
