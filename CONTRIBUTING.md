# Contribution Guidelines

## Contributing

Contributions to this project are released under a mix of the [GPLv3+](COPYING) and [BSD-3-Clause](COPYING-BSD) licenses.
Please respect the license of any existing files (specified at the top) and if adding a new file, make a value judgement which you prefer to use.

Please note that this project is released under the [Contributor Covenant Code of Conduct](CODE_OF_CONDUCT.md).
By participating in this project you agree to abide by its terms.

## Development and Testing

When developing this project, the following tools are necessary:

* Git
* One of either:
  * GCC or Clang (Clang is not strictly officially supported)
  * [`arm-none-eabi-gcc`](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/downloads) (from ARM, self-built and distro-supplied is currently broken)
* GNU Make or compatible `make` tool

Which of the compilers you pick depends on if you're going to work on the firwmare or Black Magic Debug App (BMDA) respectively.
If you wish to use the older [gnu-rm](https://developer.arm.com/downloads/-/gnu-rm) ARM toolchain, this is fine and works well.

## Common tasks

These are to be run at the root of your clone of Black Magic Probe.

* Building the firmware: `make PROBE_HOST=native` (or whichever probe you want to build for)
* Building BMDA: `make PROBE_HOST=hosted`
* Build testing all platforms: `make all_platforms`

## Submitting a pull request

### If contributing for the first time

 1. [Fork](https://github.com/blackmagic-debug/blackmagic/fork) and clone the repository
 2. Create a new branch: `git switch -c type/branch-name` (`git checkout -b type/branch-name` in the old syntax)
 3. Make your change
 4. Push to your fork and submit a [pull request](https://github.com/blackmagic-debug/blackmagic/compare)

If you wish to fix a bug, `type` in the new branch name should be `fix`, otherwise if you wish to implement a new feature, `type` should be `feature`.

### If you are working from an existing clone of the repository

1. Ensure you have our repo as a remote (`git remote add upstream https://github.com/blackmagic-debug/blackmagic`)
2. Switch back to `main` (`git switch main`/`git checkout main`)
3. Pull to ensure you're up to date (`git pull upstream`)
4. Push to your fork (`git push`)
5. Continue at 2. [in the steps for first time](#if-contributing-for-the-first-time)

### Commit message guidelines

Additionally, please write good and descriptive commit messages that both summarise the change and,
if necessary, expand on the summary using description lines.
"Patched the adiv5 code" is, while terse and correct, an example of a bad commit message.
"adiv5: Fixed an issue in DP handling in adiv5_dp_init()" is an example of a better commit message.

When writing commit messages, please prefix the component being modified using the following rules:

* If the commit modifies target support, prefix with the path under src/target including the name of the file minus its extension - for example, "adiv5:", "stm32f1:" or "flashstub/lmi:"
* If the commit modifies a platform, prefix with the name of that platform followed by the file - for example, "hosted/cli:" or "native/platform:"
* If the commit modifies a significant number of files, us the overarching theme - for example if it's a platform API change then use "platform:"
* If the commit modifies files such as the build system, the main project readme, or any other files about the project that don't form the code for the project, please use "misc:"

We would like to be able to look back through the commit history and tell what happened, when, and why without having
to dip into the commit descriptions as this improves the general Git experience and improves everyone's lives.

Try to keep commits focused on a single small and atomic change to ease review, and aid the process if we end up having to `git bisect` through your changes, or `git revert` in the extreme case something seriously broke.

We use rebasing to merge pull requests, so please keep this in mind.

## Licensing

When making contributions to existing code, we ask that you update the copyright and authorship notice at
the top with any authorship information you wish to provide. This is so you get proper attribution.
The contribution must be made under the existing code's license terms as stated per file.

When making original contributions as new files to the project, this presents a choice on licensing.
Historically the project uses GPLv3+, however contributions have been made using compatible licensing such
as MIT and BSD-3-Clause. We would ask that you preferentially choose between GPLv3+ and BSD-3-Clause.

Keep in mind that the resulting binary is GPLv3+ licensed due to how the license is worded, but the individual
contributions retain their source licensing when considering re-use in other projects.
