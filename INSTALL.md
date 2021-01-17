## Building

Pipewire uses the Meson and Ninja build system to compile. You can run it
with:

```
$ meson build
$ cd build
$ ninja
```

You can see the available meson options in `meson_options.txt` file.

If you're not familiar with these tools, the included `autogen.sh` script will
automatically run the correct `meson`/`ninja` commands, and output a Makefile.
It follows that there are two methods to build Pipewire, however both rely
on Meson and Ninja to actually perform the compilation:

```
$ ./autogen.sh
$ make
```

Provide the installation directory with the --prefix option.

## Running

If you want to run PipeWire without installing it on your system, there is a
script that you can run. This puts you in an environment in which PipeWire can
be run from the build directory, and ALSA, PulseAudio and JACK applications
will use the PipeWire emulation libraries automatically
in this environment. You can get into this environment with:

```
$ ./pw-uninstalled.sh
```

In most cases you would want to run the default pipewire daemon. Look
below for how to make this daemon start automatically using systemd.
If you want to run pipewire from the build directory, you can do this
by doing:

```
make run
```

This will use the default config file to configure and start the daemon.
The default config will also start pipewire-media-session, a default
example media session.

You can also enable more debugging with the PIPEWIRE_DEBUG environment
variable like so:

```
PIPEWIRE_DEBUG=4 make run
```

You might have to stop the pipewire service/socket that might have been
started already, with: 

```
systemctl --user stop pipewire.service
systemctl --user stop pipewire.socket
```

## Installing

PipeWire comes with quite a bit of libraries and tools, use: 

```
sudo meson install
```

to install everything onto the system into the specified prefix.
Some additional steps will have to be performed to integrate
with the distribution as shown below.

### PipeWire daemon

A correctly installed PipeWire system should have a pipewire
process and a pipewire-media-session (or alternative) process
running. PipeWire is usually started as a systemd unit using
socket activation or as a service.

Configuration of the PipeWire daemon can be found in
/etc/pipewire/pipewire.conf. Please refer to the comments in the 
config file for more information about the configuration options.

### ALSA plugin

The ALSA plugin is usually installed in:

```
/usr/lib64/alsa-lib/libasound_module_pcm_pipewire.so
```

There is also a config file installed in:

```
/usr/share/alsa/alsa.conf.d/50-pipewire.conf
```

The plugin will be picked up by alsa when the following files
are in /etc/alsa/conf.d/

```
/etc/alsa/conf.d/50-pipewire.conf -> /usr/share/alsa/alsa.conf.d/50-pipewire.conf
/etc/alsa/conf.d/99-pipewire-default.conf
```

With this setup, aplay -l should list a pipewire: device that can be used as
a regular alsa device for playback and record.

### JACK emulation

PipeWire reimplements the 3 libraries that JACK applications use to make
them run on top of PipeWire.

These libraries are found here:

```
/usr/lib64/pipewire-0.3/jack/libjacknet.so -> libjacknet.so.0
/usr/lib64/pipewire-0.3/jack/libjacknet.so.0 -> libjacknet.so.0.304.0
/usr/lib64/pipewire-0.3/jack/libjacknet.so.0.304.0
/usr/lib64/pipewire-0.3/jack/libjackserver.so -> libjackserver.so.0
/usr/lib64/pipewire-0.3/jack/libjackserver.so.0 -> libjackserver.so.0.304.0
/usr/lib64/pipewire-0.3/jack/libjackserver.so.0.304.0
/usr/lib64/pipewire-0.3/jack/libjack.so -> libjack.so.0
/usr/lib64/pipewire-0.3/jack/libjack.so.0 -> libjack.so.0.304.0
/usr/lib64/pipewire-0.3/jack/libjack.so.0.304.0

```

The provides pw-jack script uses LD_LIBRARY_PATH to set the library
search path to these replacement libraries. This allows you to run
jack apps on both the real JACK server or on PipeWire with the script.

It is also possible to completely replace the JACK libraries by adding
a file `pipewire-jack-x86_64.conf` to `/etc/ld.so.conf.d/` with
contents like:

```
/usr/lib64/pipewire-0.3/jack/
```

Note that when JACK is replaced by PipeWire, the SPA JACK plugin (installed
in /usr/lib64/spa-0.2/jack/libspa-jack.so) is not useful anymore and
distributions should make them conflict.


### PulseAudio replacement

PipeWire reimplements the PulseAudio server protocol as a small service
that runs on top of PipeWire.

The binary is normally placed here:

```
/usr/bin/pipewire-pulse
```

The server can be started with provided systemd activation files or
from PipeWire itself. (See `/etc/pipewire/pipewire.conf`)

```
systemctl --user stop pipewire-pulse.service
systemctl --user stop pipewire-pulse.socket
```

You can also start additional PulseAudio servers listening on other
sockets with the -a option. See `pipewire-pulse -h` for more info.
