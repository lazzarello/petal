{ pkgs, lib, config, inputs, ... }:

{
  # Linux native dependencies of Tauri/WebKitGTK, LiveKit/WebRTC, and cpal.
  packages = [ pkgs.github-cli ] ++ lib.optionals pkgs.stdenv.isLinux (with pkgs; [
    pkg-config
    ninja
    gobject-introspection
    gst_all_1.gstreamer
    gst_all_1.gst-plugins-base
    gst_all_1.gst-plugins-good
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

  languages.python = {
    enable = pkgs.stdenv.isLinux;
    uv.enable = true;
    lsp.enable = true;
    venv.enable = true;
  };

  languages.rust.enable = true;
  languages.typescript.enable = true;
  languages.swift.enable = pkgs.stdenv.isDarwin;
}
