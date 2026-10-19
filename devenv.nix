{ pkgs, ... }:

{
  # Linux native dependencies of Tauri/WebKitGTK, LiveKit/WebRTC, and cpal.
  packages = with pkgs; [
    rustc
    cargo
    nodejs_24 # build.rs builds the Vite frontend before checking Rust
    pkg-config
    clang
    cmake
    glib
    gtk3
    webkitgtk_4_1
    libsoup_3
    libayatana-appindicator
    dbus
    alsa-lib
    openssl
    libva
    pipewire
  ];
}
