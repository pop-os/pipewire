Manual tests for PipeWire
=========================

Introspection (pipewire)
------------------------

Install `pipewire`.

Run `pw-dump'`. It should print a lot of information. Check at least
the version is correct. With `jq` installed for example,

    pw-dump | jq '.[0].info.version'

Video streams
-------------

Install `pipewire` and `pipewire-tests`.

Run `/usr/libexec/installed-tests/pipewire-0.3/examples/video-src`. It
will print a node ID, for example 42.

In another terminal, run
`/usr/libexec/installed-tests/pipewire-0.3/examples/video-play 42` or
`/usr/libexec/installed-tests/pipewire-0.3/examples/export-sink 42`,
replacing 42 by the node ID you saw. You should get a window with an
animation. Try this with
`/usr/libexec/installed-tests/pipewire-0.3/examples/video-play-reneg 42`
well. This time, the animation pattern should change every second
or so.

Copy the package configuration into the system configuration area,

```
sudo mkdir /etc/pipewire
sudo cp /usr/share/pipewire/pipewire.conf /etc/pipewire/pipewire.conf
```

Apply this configuration diff to enable the videotestsrc,

```
$ diff /usr/share/pipewire/pipewire.conf /etc/pipewire/pipewire.conf
88c88
<     #videotestsrc   = videotestsrc/libspa-videotestsrc
---
>     videotestsrc   = videotestsrc/libspa-videotestsrc
267c267
<     #{ factory = spa-node-factory   args = { factory.name = videotestsrc node.name = videotestsrc node.description = videotestsrc node.param.Props = { patternType = 1 } } }
---
>     { factory = spa-node-factory   args = { factory.name = videotestsrc node.name = videotestsrc node.description = videotestsrc node.param.Props = { patternType = 1 } } }
```

Restart the daemons with `systemctl --user restart pipewire{,-pulse}{.service,.socket} wireplumber`.

You should see a node in the output of `pw-cli ls Node` with
`node.name = "videotestsrc"`. Pass its node ID to `video-play` to see
a different animation.

V4L2 cameras
------------

If you have a camera, run
`/usr/libexec/installed-tests/pipewire-0.3/examples/local-v4l2` or
`/usr/libexec/installed-tests/pipewire-0.3/examples/spa/local-v4l2`.
You should get a camera stream displayed in a window (but this might
fail if it cannot negotiate a suitable capture resolution).

Audio sink
----------

Ensure `pw-play /usr/share/sounds/alsa/Front_Center.wav` plays back
correctly.

Audio test source
-----------------

Prepare to modify the default configuration,

```
sudo mkdir /etc/pipewire
sudo cp /usr/share/pipewire/pipewire.conf /etc/pipewire/pipewire.conf
```

Apply this configuration diff to enable the audiotestsrc,

```
$ diff /usr/share/pipewire/pipewire.conf /etc/pipewire/pipewire.conf
89c89
<     #audiotestsrc   = audiotestsrc/libspa-audiotestsrc
---
>     audiotestsrc   = audiotestsrc/libspa-audiotestsrc
271c271
<     #{ factory = adapter            args = { factory.name = audiotestsrc node.name = my-test node.description = audiotestsrc node.param.Props = { live = false }} }
---
>     { factory = adapter            args = { factory.name = audiotestsrc node.name = my-test node.description = audiotestsrc } }
```

then you should see a node in the output of `pw-cli ls Node` with
`node.name = "my-test"`. You can record from it with `pw-record --target ${node id here} test.wav`
(press Ctrl+C to stop recording).

ALSA client plugin (pipewire-alsa)
----------------------------------------------------

Install `pipewire-alsa` and `alsa-utils`.
Make sure PulseAudio is not currently playing audio and is configured
to release the audio device when not in use.

`aplay -L` should list `pipewire`.

`aplay -D pipewire /usr/share/sounds/alsa/Front_Center.wav` should
play a sound.

JACK client library replacement (pipewire-jack)
-----------------------------------------------------------------

Install `pipewire`, `pipewire-jack`, `alsa-utils` and
`sndfile-tools`. Do not have a real JACK server running.
Make sure PulseAudio is not currently playing audio and is configured
to release the audio device when not in use.

`aplay -D jack /usr/share/sounds/alsa/Front_Center.wav` and
`sndfile-jackplay /usr/share/sounds/alsa/Front_Center.wav` should fail
with:

```
jack server is not running or cannot be started
```

`pw-jack aplay -D jack /usr/share/sounds/alsa/Front_Center.wav`
should succeed.

* TODO: Currently it prints

        aplay: set_params:1343: Sample format non available
        Available formats:
        - FLOAT_LE

`pw-jack sndfile-jackplay /usr/share/sounds/alsa/Front_Center.wav`
should succeed.

PulseAudio client library replacement (pipewire-pulse)
-----------------------------------------------------------------------

Install `pipewire`, `pipewire-pulse`, `alsa-utils` and
`pulseaudio-utils`. Make sure PulseAudio is not currently playing audio
and is configured to release the audio device when not in use.

Setup: let your pulseaudio service become idle, then
`pkill -STOP pulseaudio`.

`paplay /usr/share/sounds/alsa/Front_Center.wav` should hang (because
PulseAudio has been stopped).

Teardown: `pkill -CONT pulseaudio` to return it to normal.

GStreamer elements (gstreamer1.0-pipewire)
------------------------------------------

Install `gstreamer1.0-tools` and `gstreamer1.0-pipewire`.
Make sure PulseAudio is not currently playing audio and is configured
to release the audio device when not in use.

Run: `gst-inspect-1.0 pipewire`. It should list `pipewiresrc`,
`pipewiresink` and `pipewiredeviceprovider`.

Run: `gst-inspect-1.0 pipewiresrc`. It should list details.

Run: `gst-inspect-1.0 pipewiresink`. It should list details.

Run: `gst-launch-1.0 audiotestsrc '!' pipewiresink`. It should beep
until you press Ctrl+C.

Go through the launch lines in `gst-device-monitor-1.0 Video/Source`
and `gst-device-monitor-1.0 Audio/Source`. For example, given this
under `Video/Source`,

    gst-launch-1.0 pipewiresrc target-object=106 ! ...

Try,

    gst-launch-1.0 pipewiresrc target-object=106 ! videoconvert ! autovideosink

Similarly, `... ! audioconvert ! audioresample ! autoaudiosink` can be used
for `Audio/Source`s.
