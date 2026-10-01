# Contributing

Most useful contribution: **compatibility reports**. Run `sudo lampctl probe` and, if it
works or fails, open an issue using the *Compatibility report* template. A new descriptor
dump in `tests/descriptors/` plus an expectation in `tests/run.sh` is a great PR.

Guidelines
* Plain C99, no dependencies beyond libc and Linux headers.
* `make test` must pass; build must be clean with `-Wall -Wextra`.
* This project is vibe-coded. AI-assisted PRs are welcome, but please say so and make
  sure you've actually run the code on a device or against a descriptor.
