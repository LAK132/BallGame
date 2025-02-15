# BallGame

## Building

Requires [meson](https://mesonbuild.com/)

### Windows

`setup.bat msvc --buildtype=release && compile.bat ballgame && build\\ballgame.exe`

### WSL

`./win_setup.sh msvc --buildtype=release && ./win_compile.sh ballgame && ./build/ballgame.exe`

### Linux/Mac

gcc: `./setup.sh gcc --buildtype=release && ./compile.sh ballgame && ./build/ballgame`

clang: `./setup.sh clang --buildtype=release && ./compile.sh ballgame && ./build/ballgame`
