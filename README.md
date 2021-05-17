# vb.mi-dev

Several MaxMSP clones of eurorack modules originally created by 'mutable instruments' https://mutable-instruments.net/ 

Many thanks to Émilie Gillet for making the source code available!
https://github.com/pichenettes/eurorack

**Please note, this is not a release of 'mutable instruments'!**

Volker Böhm, 2019
https://vboehm.net



Compiled versions for macOS can be found here:

https://vboehm.net/downloads (look for vb.mi-objects)



## Building

(Mostly copying from https://github.com/Cycling74/max-devkit ReadMe.md)

0. Clone the code from Github, including submodules: 
   `git clone --recurse-submodules https://github.com/v7b1/vb.mi-dev.git`

1. `cd vb.mi-dev` to change directories (cd) into the folder

2. ##### Build libsamplerate

   - `cd source/libs/libsamplerate`
   - `mkdir build && cd build`
   - `cmake -DLIBSAMPLERATE_EXAMPLES=OFF -DBUILD_TESTING=OFF ..` 
   - `cmake --build . --config 'Release'`
   - `cd ../../../..`

3. `mkdir build` to create a folder with your various build files

4. `cd build` to put yourself into that folder

5. Now you can generate the projects for your chosen build environment:

### Mac 

You can build on the command line using Makefiles, or you can generate an Xcode project and use the GUI to build.

* Xcode: Run `cmake -G Xcode ..` and then run `cmake --build . --config 'Release'` or open the Xcode project from this "build" folder and use the GUI.
* Make: Run `cmake ..` and then run `cmake --build . --config 'Release'`.  Note that the Xcode project is preferrable because it is able to substitute values for e.g. the Info.plist files in your builds.

### Windows

Note: this is untested, but should work something like this:

`cmake ..` 

`cmake --build . --config 'Release'`

