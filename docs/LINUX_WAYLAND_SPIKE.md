# Linux Wayland build + capture spike

Scope: a **standalone local viewer**, with the system ScreenCast portal owning consent/source selection and PipeWire owning video transport. No Petal room, LiveKit publication, custom picker, audio, or remote control. The prototype is `apps/desktop/scripts/wayland-capture-spike.py`; it does not enable any Linux Tauri commands.

## Reproduce

On a Wayland desktop with a ScreenCast portal backend and PipeWire, install Python 3, PyGObject/Gio + GStreamer introspection, GStreamer core/base/good, and the PipeWire GStreamer plugin. On Ubuntu, the relevant packages include `python3-gi`, `gir1.2-gstreamer-1.0`, `gstreamer1.0-tools`, `gstreamer1.0-plugins-base`, `gstreamer1.0-plugins-good`, and `gstreamer1.0-pipewire` (plus the desktop's portal backend). From the repo root:

```sh
python3 apps/desktop/scripts/wayland-capture-spike.py --self-test
python3 apps/desktop/scripts/wayland-capture-spike.py
```

The first command does not request screen access: it checks the portal interface/plugins and runs a three-frame synthetic pipeline. The second calls `CreateSession`, `SelectSources`, `Start`, and `OpenPipeWireRemote`; choose a non-sensitive source in the desktop consent dialog. It opens a local viewer if negotiation succeeds. Ctrl-C closes the portal session. Do not run on a desktop with private content visible unless you intend to share it.

For the Petal Rust build, use the repo-root Linux `devenv` (its Rust toolchain and native libraries are pinned in `devenv.lock`):

```sh
devenv shell
cd apps/desktop
npm ci
cd src-tauri
cargo check --locked --lib
```

`devenv info` lists the selected toolchain/libraries without building the shell. The first `devenv shell` may need large WebKitGTK and libwebrtc downloads; `npm ci` is required because Rust's `build.rs` runs Vite if frontend assets are missing. The environment provides build prerequisites, **not** a Linux implementation of the macOS/Windows-gated Rust code. Do not treat a successful shell entry as a successful Petal build.

## Results on the current NixOS/niri Wayland host

- `--self-test`: **PASS** (portal interface present, PipeWire GStreamer source present, synthetic video pipeline reaches EOS).
- Portal `CreateSession` / `SelectSources` / `Start` / `OpenPipeWireRemote`: **completed** and returned one node and an fd. The probe closes both on exit.
- Real video viewer: **BLOCKED** at GStreamer/PipeWire format negotiation: `stream error: no more input formats` from `gstpipewiresrc.c:929`, both with unconstrained raw video and with a BGRA caps filter. No frames/viewer success has been demonstrated; investigate compositor/portal-advertised formats versus the consumer and repeat on the named target desktop before integrating it.
- `cargo check --locked --lib`: With the devenv shell and `npm ci`, native dependencies and the Vite build passed; **Rust compilation still fails with 114 errors**. Most are Windows-only modules referenced unconditionally on Linux (`session_stub.rs`, `lib.rs`, `windows_compositor`, `windows_hover`). The 14 missing-`libc` errors in `platform/fd.rs` were fixed by declaring the dependency for Linux. This is a source-portability blocker, not another missing system dependency. The earlier bare-shell attempt stopped at `gio-sys` because `pkg-config` was absent.

A successful portal handshake is **not** a share/publish proof. Next go/no-go gates: separate the Windows session/command surface from Linux and fix the Linux crate build; receive real frames on a supported Ubuntu Wayland desktop; only then evaluate wiring frames into `transport::publisher` and a Linux viewer. Do not claim Linux support or enable Linux release targets based on this spike.
