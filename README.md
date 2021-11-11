# temu

A hardware-accelerated UTF-8 terminal emulator for POSIX environments

## About

This is a personal exploratory project that I intend to officially release (and use myself).
As of now, you should be able to run rudimentary commands (with colored and bold/italic text), edit inline, scroll through the terminal's history, and load non-bitmap, scalable TTF/OTF fonts.
However, many required features haven't been implemented yet - most notably, the alt-screen (for programs like "less" and "vim"), signal-handling, mouse selection, and a number of common escape sequences and modes. Additionally, the codebase is in constant flux.
As such, you shouldn't use this for anything resembling real work (yet).

## Platforms

This application targets Linux, OpenBSD, and FreeBSD - however, I've only tested it on Linux.
Currently, only X11 is supported but a Wayland build is planned - and the architecture was designed with that in mind.

## Dependencies

- [GCC], [Clang], or any C compiler with C11 support
- [CMake] v3.7
- [Xlib]
- [OpenGL ES] v3.2
- [FreeType] v2
- [FontConfig]

## Quick Start

```console
$ ./setup.sh
$ cd build
$ make
$ ./temu
```
or, manually:

```console
$ mkdir build
$ cd build
$ cmake ..
$ make
$ ./temu
```
## Key Bindings

Actual key bindings have not been implemented yet, but you can scroll up/down with ALT-U/ALT-D, respectively.

[GCC]: https://gcc.gnu.org/
[Clang]: https://clang.llvm.org/
[CMake]: https://cmake.org/
[Xlib]: https://www.x.org/wiki/
[OpenGL ES]: https://www.khronos.org/opengles/
[FreeType]: https://freetype.org/
[FontConfig]: https://www.freedesktop.org/wiki/Software/fontconfig/

