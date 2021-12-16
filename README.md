# temu

A hardware-accelerated UTF-8 terminal emulator for POSIX environments

## About

This is a personal exploratory project that I intend to officially release (and use myself).
As of now, you should be able to run rudimentary commands (with colored and bold/italic text), edit inline, scroll through the terminal's history, and load non-bitmap, scalable TTF/OTF fonts.
However, many required features haven't been implemented yet - most notably, mouse selection and a number of common escape sequences and terminal modes. Additionally, the codebase is in constant flux.
As such, you shouldn't use this for anything resembling real work (yet).

## Platforms

This application targets Linux, OpenBSD, and FreeBSD - however, I've only tested it on Linux.
Currently, only X11 is supported but a Wayland build is planned - and the architecture was designed with that in mind.

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

Proper key bindings have not been implemented yet, but you can scroll up/down with ALT-k/j, and page up/down with SHIFT-PageUp/PageDown.

Note that the scrollback buffer is kept abnormally small by default for debugging purposes. You can configure the number of saved lines with the "-m" command-line option.

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

