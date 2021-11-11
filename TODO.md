# TODO

## Short-term

- [ ] Improve error handling/recovery everywhere
- [ ] Standardized error messages
- [ ] Standardized log levels/messages
- [ ] Prepare the codebase for leak detection tools
- [ ] Cleanup the utilities and link as a static library
- [ ] Implement the alt-screen
- [ ] Signal-handling for CTRL-C/CTRL-D
- [ ] Signal-handling for external program queries
- [ ] Second pass on keypress-triggered escape strings
- [ ] Move the entry point and cleanup the mess in main.c
- [ ] Gracefully handle RGB glyphs in the freetype loader
- [ ] Handle loading non-scalable bitmap fonts
- [ ] Use a better heuristic for estimating fixed font widths
- [ ] Think of a better method of storing cell colors and passing them to renderer
- [ ] Separate line and cell buffers in history ring for easier renderer access
- [ ] API for setting window title/properties (exposed to the terminal)
- [ ] Set window size after getting font metrics so we don't have to display the window before the terminal even starts
- [ ] Robust keyboard -> keycode mappings for international/non-standard keyboard layouts
- [ ] Mouse cursor, selection, and copy/paste
- [ ] Signal-handling for external program queries
- [ ] Integrate optparse.h for command line arguments
- [ ] Re-integrate modifiable cursor shape/color
- [ ] Key bindings
- [ ] License preambles in source files
- [ ] Serializer for history buffer to record sessions (for debugging)
- [ ] Platform abstraction for window events

## Mid-term

- [ ] Switch to using the POSIX-compliant API for PTY creation
- [ ] Acheive full xterm compatibility
- [ ] User config file
- [ ] RGB glyphs
- [ ] Font substitution for missing glyphs
- [ ] Zoom functionality (i.e. glyph scaling)
- [ ] Find out how to minimize frames when PTY input is still pending
- [ ] Integrate standardized VT tests
- [ ] Testing on non-Linux platforms

