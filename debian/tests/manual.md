Manual tests for PipeWire
=========================

Introspection (pipewire)
------------------------

Install `pipewire`.

Run `pw-cli dump`.

ALSA client plugin (pipewire-audio-client-libraries)
----------------------------------------------------

Install `pipewire-audio-client-libraries` and `alsa-utils`.
Make sure PulseAudio is not currently playing audio and is configured
to release the audio device when not in use.

`aplay -L` should list `pipewire`.

`aplay -D pipewire /usr/share/sounds/alsa/Front_Center.wav` should
play a sound.

JACK client library replacement (pipewire-audio-client-libraries)
-----------------------------------------------------------------

Install `pipewire`, `pipewire-audio-client-libraries`, `alsa-utils` and
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

    and segfaults.

`pw-jack sndfile-jackplay /usr/share/sounds/alsa/Front_Center.wav`
should succeed.

* TODO: Currently it prints
    `Cannot connect output port 0 (alsa_pcm:playback_1)` and plays
    silence for the length of the test file.

PulseAudio client library replacement (pipewire-audio-client-libraries)
-----------------------------------------------------------------------

Install `pipewire`, `pipewire-audio-client-libraries`, `alsa-utils` and
`pulseaudio-utils`. Make sure PulseAudio is not currently playing audio
and is configured to release the audio device when not in use.

Setup: let your pulseaudio service become idle, then
`pkill -STOP pulseaudio`.

`paplay /usr/share/sounds/alsa/Front_Center.wav` should hang (because
PulseAudio has been stopped).

`pw-pulse paplay /usr/share/sounds/alsa/Front_Center.wav` should play
the audio.

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

Run: `gst-launch-1.0 pipewiresrc '!' videoconvert '!' autovideosink`.
You should get a webcam image (if you have a webcam).

Bluetooth as audio backend (libspa-0.2-bluetooth)
-------------------------------------------------

TODO

JACK as audio backend (libspa-0.2-jack)
---------------------------------------

TODO

audiotestsrc, videotestsrc
--------------------------

TODO
