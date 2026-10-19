#!/usr/bin/env python3
"""Standalone Wayland ScreenCast portal -> PipeWire -> local video viewer spike.

Run --self-test first. This is deliberately NOT wired into Petal or LiveKit.
Requires Python 3 + PyGObject (Gio/Gst introspection), GStreamer core/base/good,
PipeWire's GStreamer plugin, and a working ScreenCast portal backend.
"""

import argparse
import os
import sys

import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gio, GLib, Gst

PORTAL = "org.freedesktop.portal.Desktop"
OBJECT = "/org/freedesktop/portal/desktop"
INTERFACE = "org.freedesktop.portal.ScreenCast"
REQUEST = "org.freedesktop.portal.Request"


def call(bus, method, signature, values, interface=INTERFACE):
    return bus.call_sync(
        PORTAL, OBJECT, interface, method, GLib.Variant(signature, values),
        None, Gio.DBusCallFlags.NONE, 30_000, None,
    ).unpack()


def request(bus, method, signature, values):
    # Listen before issuing the request: a quick response can beat call_sync.
    responses = {}
    loop = GLib.MainLoop()

    def on_response(_bus, _sender, path, _interface, _signal, parameters):
        responses[path] = parameters.unpack()
        if path == responses.get("expected"):
            loop.quit()

    sub = bus.signal_subscribe(PORTAL, REQUEST, "Response", None, None,
                               Gio.DBusSignalFlags.NONE, on_response)
    try:
        path, = call(bus, method, signature, values)
        responses["expected"] = path
        if path not in responses:
            waiting = {"timed_out": False}
            def timed_out():
                waiting["timed_out"] = True
                loop.quit()
                return GLib.SOURCE_REMOVE
            timer = GLib.timeout_add_seconds(90, timed_out)
            try:
                loop.run()
            finally:
                if not waiting["timed_out"]:
                    GLib.source_remove(timer)
        if path not in responses:
            raise TimeoutError(f"{method}: portal did not respond")
        status, result = responses[path]
        if status != 0:
            raise RuntimeError(f"{method}: portal declined/cancelled (status {status})")
        return result
    finally:
        bus.signal_unsubscribe(sub)


def pipeline_for(fd, node):
    # node is the portal-returned uint32, never an untrusted launch string.
    return Gst.parse_launch(
        f"pipewiresrc fd={fd} path={node} do-timestamp=true ! "
        "videoconvert ! autovideosink sync=false"
    )


def self_test(bus):
    for element in ("pipewiresrc", "videoconvert", "autovideosink", "videotestsrc", "fakesink"):
        assert Gst.ElementFactory.find(element), f"missing GStreamer plugin: {element}"
    call(bus, "Get", "(ss)", (INTERFACE, "version"), "org.freedesktop.DBus.Properties")
    pipe = Gst.parse_launch("videotestsrc num-buffers=3 ! videoconvert ! fakesink")
    assert pipe.set_state(Gst.State.PLAYING) != Gst.StateChangeReturn.FAILURE
    message = pipe.get_bus().timed_pop_filtered(10 * Gst.SECOND, Gst.MessageType.EOS | Gst.MessageType.ERROR)
    pipe.set_state(Gst.State.NULL)
    assert message and message.type == Gst.MessageType.EOS, "synthetic video pipeline failed"
    print("PASS: portal interface, PipeWire plugin and synthetic video pipeline available")


def view(bus):
    session = None
    fd = None
    pipe = None
    try:
        token = f"petal_{os.getpid()}"
        created = request(bus, "CreateSession", "(a{sv})", ({
            "handle_token": GLib.Variant("s", token + "_create"),
            "session_handle_token": GLib.Variant("s", token + "_session"),
        },))
        session = created["session_handle"]
        request(bus, "SelectSources", "(oa{sv})", (session, {
            "handle_token": GLib.Variant("s", token + "_select"),
            "types": GLib.Variant("u", 3),  # portal screen or window
            "multiple": GLib.Variant("b", False),
        }))
        started = request(bus, "Start", "(osa{sv})", (session, "", {
            "handle_token": GLib.Variant("s", token + "_start"),
        }))
        streams = started.get("streams", [])
        if len(streams) != 1:
            raise RuntimeError(f"expected one video stream, got {len(streams)}")
        node = streams[0][0]
        if not isinstance(node, int) or not 0 <= node <= 0xffffffff:
            raise ValueError("portal returned invalid PipeWire node id")
        reply, fds = bus.call_with_unix_fd_list_sync(
            PORTAL, OBJECT, INTERFACE, "OpenPipeWireRemote",
            GLib.Variant("(oa{sv})", (session, {})), GLib.VariantType.new("(h)"),
            Gio.DBusCallFlags.NONE, 30_000, None, None,
        )
        fd = fds.get(reply.unpack()[0])
        pipe = pipeline_for(fd, node)
        if pipe.set_state(Gst.State.PLAYING) == Gst.StateChangeReturn.FAILURE:
            message = pipe.get_bus().timed_pop_filtered(Gst.SECOND, Gst.MessageType.ERROR)
            if message:
                error, detail = message.parse_error()
                raise RuntimeError(f"PipeWire viewer failed to start: {error}: {detail}")
            raise RuntimeError("PipeWire viewer failed to start (no GStreamer error)")
        print("Portal-selected stream is open; close the viewer or press Ctrl-C to stop.")
        bus_messages = pipe.get_bus()
        while True:
            message = bus_messages.timed_pop_filtered(
                Gst.SECOND, Gst.MessageType.ERROR | Gst.MessageType.EOS,
            )
            if message and message.type == Gst.MessageType.ERROR:
                error, detail = message.parse_error()
                raise RuntimeError(f"video pipeline: {error}: {detail}")
            if message and message.type == Gst.MessageType.EOS:
                break
    finally:
        if pipe:
            pipe.set_state(Gst.State.NULL)
        if fd is not None:
            os.close(fd)
        if session:
            try:
                bus.call_sync(PORTAL, session, "org.freedesktop.portal.Session", "Close",
                              None, None, Gio.DBusCallFlags.NONE, 5_000, None)
            except GLib.Error as error:
                print(f"could not close portal session: {error}", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="no capture or consent dialog")
    args = parser.parse_args()
    Gst.init(None)
    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    if args.self_test:
        self_test(bus)
    else:
        view(bus)


if __name__ == "__main__":
    try:
        main()
    except (GLib.Error, RuntimeError, TimeoutError, ValueError, AssertionError) as error:
        sys.exit(f"spike failed: {error}")
