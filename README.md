# temu

A hardware-accelerated UTF-8 terminal emulator for POSIX environments

## About

This is a personal exploratory project that I intend to officially release (and use myself).
As of now, you should be able to run most commands (with colored and bold/italic text), edit inline,
scroll through the terminal's history, and load scalable TTF/OTF fonts.
However, many required features haven't been implemented yet - most notably, mouse selection and a
number of common escape sequences and terminal modes - especially with regard to TUI programs.
As such, you shouldn't use this for anything resembling real work (yet).

## Platforms

This application targets Linux, OpenBSD, and FreeBSD - however, I've only tested it on Linux.
Currently, only X11 is supported but it appears to run fine under XWayland as well. That being said,
a proper native Wayland backend will be implemented at some point.

Note that you may encounter problems running this in a 3D-accelerated virtual environment due to limited
driver support for OpenGL (irrespective of your hardware). This will be fixed in the future.

## Dependencies

- [GCC], [Clang], or any C compiler with C11 support
- [CMake] v3.7
- [Mesa] and a GPU that supports [OpenGL ES] v3.0 or higher
- [Xlib]
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
$ cmake ../
$ make
$ ./temu
```
## Key Bindings

Proper key bindings have not been implemented yet, but you can scroll up/down with ALT-k/j, and page
up/down with SHIFT-PageUp/PageDown.

Note that the scrollback buffer is kept abnormally small by default for debugging purposes. You can
configure the number of saved lines with the "-m" command-line option.

## Licensing

The core terminal implementation is licensed under the GPL-3. Any derived modules that become standalone
libraries will likely be re-released under a permissive license. Such changes will be reflected in this
repository.

## Resources

- Overview of [terminal emulators](https://en.wikipedia.org/wiki/Terminal_emulator) and [their escape codes](https://en.wikipedia.org/wiki/ANSI_escape_code)
- [State-machine diagram for a compliant ANSI escape sequence parser](https://vt100.net/emu/dec_ansi_parser)
- [XTerm's supported escape sequences, with descriptions](https://www.xfree86.org/current/ctlseqs.html)

[GCC]: https://gcc.gnu.org/
[Clang]: https://clang.llvm.org/
[CMake]: https://cmake.org/
[Mesa]: https://www.mesa3d.org/
[OpenGL ES]: https://www.khronos.org/opengles/
[Xlib]: https://www.x.org/wiki/
[FreeType]: https://freetype.org/
[FontConfig]: https://www.freedesktop.org/wiki/Software/fontconfig/

