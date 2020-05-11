# vb.mi-dev

Several MaxMSP clones of eurorack modules originally created by 'mutable instruments' https://mutable-instruments.net/ 

Thanks to Émilie Gillet for making the source code available!
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
2. `mkdir build` to create a folder with your various build files
3. `cd build` to put yourself into that folder
4. Now you can generate the projects for your chosen build environment:

### Mac 

You can build on the command line using Makefiles (not tested, yet), or you can generate an Xcode project and use the GUI to build.

* Xcode: Run `cmake -G Xcode ..` and then run `cmake --build .` or open the Xcode project from this "build" folder and use the GUI.
* Make: Run `cmake ..` and then run `cmake --build .` or `make`.  Note that the Xcode project is preferrable because it is able to substitute values for e.g. the Info.plist files in your builds.

### Windows

Note: this is untested, but should work something like this:

If you are using Visual Studio, You can run `cmake --help` to get a list of the options available.  Assuming some version of Visual Studio 2017, the commands to generate the projects will look like this:

* 32 bit: `cmake -G "Visual Studio 15 2017" ..`
* 64 bit: `cmake -G "Visual Studio 15 2017 Win64" ..`

Having generated the projects, you can now build by opening the .sln file in the build folder with the Visual Studio app (just double-click the .sln file) or you can build on the command line like this:

`cmake --build . --config Release`