{ pkgs, lib, config, inputs, ... }:

{
  # Linux native dependencies of Tauri/WebKitGTK, LiveKit/WebRTC, and cpal.
  packages = [ pkgs.github-cli ] ++ lib.optionals pkgs.stdenv.isLinux (with pkgs; [
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
  ]);

  # why didn't the robot use the languages feature of devenv?
  languages.rust.enable = true;
  languages.typescript.enable = true;
  languages.swift.enable = pkgs.stdenv.isDarwin;
}
