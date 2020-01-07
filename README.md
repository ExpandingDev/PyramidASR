# PyramidASR
A DBus interface for CMU Pocketsphinx speech recognition.

## Compilation
Pyramid ASR relies upon the following libraries to run
- dbus-cxx *For DBus interface*
- pocketsphinx *Speech recognition*
- sphinxbase *Getting audio device for speech recognition*
- GLib 2.0 *Parsing configuration files*

Pyramid ASR uses a CMake build system, to build and install:  

    cmake CMakeLists.txt
    make -j 4
    sudo make install
    
## Purpose
The main goal of Pyramid ASR is to function as a daemon for speech recognition so that other programs running on your device can hook into speech to text control without needing a large speech recognition library. Having this daemon running in the background on statup ensures that the speech to text ability is loaded when it is needed.

## Usage
	pyramid [ -h | -v | -d | -a ADDRESS ]

	-h		Show this usage text.
	-d		Start a new Pyramid ASR Service daemon if one is not running already.
	-v		Display the current version of this program.
	-a ADDRESS	Connect the Pyramid ASR Service to the specified custom DBus bus specified by the given address.

If no options are supplied, a new Pyramid ASR Service instance will be made unless another Pyramid ASR Service instance is already running.  
Daemonization will fail if another Pyramid ASR Service instance is running.  
Addresses specified by the -a option should be DBus specification compliant, for example: `pyramid -a unix:/tmp/pyramid.socket`  
Specifies to connect to a DBus through a UNIX socket located at `/tmp/pyramid.socket`



## DBus Interface
All of the interfaces implemented by Pyramid ASR are described with the DBus introspection format in the `ca.l5.expandingdev.PyramidASR` file in the `res/` subdirectory. Additional documentation as to what each method does and usage examples are to come.
