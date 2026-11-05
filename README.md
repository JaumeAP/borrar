# Introduction

IABLib is a C++ library for creating, parsing and rendering IAB essence, as
specified in SMPTE ST 2098-2.

# License

See [LICENSE](./LICENSE).

# Documentation

Documentation is provided inline using [Doxygen](https://www.doxygen.nl/)
conventions. The build system can generate an HTML rendering.

# Examples

The [unit tests](./tests) and the [render-to-files application](./src/apps/)
illustrate the use of the library.

# Layouts (this fork)

[layouts/](./layouts) makes renderer configurations (`.cfg`) for real rooms from their
measurements, with the angles IABLib's own transform gives each speaker's place in the
room (`cubeangles`). Its generator made
[configurations/IAB_cinema_11x6.5.array.vbap.cfg](./configurations/IAB_cinema_11x6.5.array.vbap.cfg).
Design in [layouts/docs/design.md](./layouts/docs/design.md).

# Dependencies

The library has no runtime dependencies.

The following development dependencies exist:

* [GoogleTest](https://github.com/google/googletest) (unit tests)
* [Doxygen](https://www.doxygen.nl/) (documentation)
