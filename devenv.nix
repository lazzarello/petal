{ pkgs, lib, config, inputs, ... }:

{
  # Linux native dependencies of Tauri/WebKitGTK, LiveKit/WebRTC, and cpal.
  packages = with pkgs; [
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
    github-cli
  ];

  # why didn't the robot use the languages feature of devenv?
  languages.rust.enable = true;
  languages.typescript.enable = true;
}
