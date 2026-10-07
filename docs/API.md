# C library reference

Include `dock.h` and link `libdock.a` with IOKit/CoreFoundation, or link
`libdock.dylib` with a suitable `@rpath`. Homebrew installs headers under
`$(brew --prefix dockctl)/include/dockctl` and libraries under its `lib` directory.
The `module.modulemap` exports module `Dock`.
Use matching header/binary versions; `DOCK_API_VERSION` is 2.

## Ownership and errors

`dock_read_info()` and `dock_read_host_power()` do not require a HPM context.
`dock_open()` requires root, recognizes the firmware, discovers exactly one Dell
HPM partner and holds a process lock. Release it with `dock_close()` on every
path. Contexts must not be shared between concurrent calls. Errors are returned
as statuses and optionally described in `dock_error.message`; the library does
not print output.

`dock_close()` attempts automatic restoration if this context changed the mode
without confirming restoration. It returns no status: call `dock_automatic()`
explicitly when your application needs a reported result. No cleanup can
recover after SIGKILL or a lost connection. `dock_enter()` is an explicit action;
neither open nor reads automatically enter the Dell mode. There is no Exit API.

## Reads and decoding

| Function | Output |
|---|---|
| `dock_read_info` | Identity, firmware descriptors, raw fields, host charger |
| `dock_read_host_power` | Typed AppleSmartBattery values, each with a `known` flag |
| `dock_read_thermal` | Mode, speed class, local/remote/module temperatures |
| `dock_read_registers` | Number of filled register entries, status/length per entry |
| `dock_decode_pd` | Pure decoding of captured PDO/RDO data, no hardware I/O |
| `dock_rid` | Discovered HPM RID, or -1 |
| `dock_monotonic_ms` | Monotonic timestamp for local scheduling |

Successful thermal reads combine two sequential exchanges. `observed_ms` is the
completion time, not the sensor's own measurement timestamp. Power values may
be cached, and do not measure dock-wide consumption. A known offer list can
coexist with an unknown contract: check `contract_inferred`, not just the status.

## Watcher

Start from `dock_watch_defaults()` and validate with
`dock_watch_options_valid()`. Temperature arrays are local, remote, module.
The README documents option ranges. `dock_watch()` uses the same policy as the
CLI; the cancellation callback should only return a flag.

The update callback receives `const dock_watch_event *`:

- `DOCK_SAMPLE`: observed state with optional pending stability timer.
- `DOCK_TRANSITION`: requested change, emitted **before** writing. `sample` is
  the reading that triggered it; `target` is not yet confirmed.
- `DOCK_RESTORE`: restoration request after cancellation, expiry or error;
  the retained reading can be stale, as indicated by `sample_age_ms`.
- `DOCK_END`: fresh final automatic reading after successful verification.

`elapsed_ms` is session age; `state_elapsed_ms` is time in the committed state.
`pending_reason` and `pending_elapsed_ms` describe a condition being confirmed.
Callbacks execute synchronously, so blocking one blocks monitoring/restoration.
No final event is promised when restoration or the final read fails.

`dock_policy_next()` performs no I/O and updates the policy's pending timer.
The caller must commit `state` and `changed_ms` only after applying a transition.
The runner uses this shared logic through injectable internal I/O for simulations;
that test interface is not part of the public ABI.

The example `examples/watch.c` really changes fan mode when run with root and
an active Dell session. It restores on ordinary cancellation/error, but does
not supply an autonomous hardware watchdog or a privileged GUI helper.
