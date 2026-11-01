Your job is port SillyTavern to C/C++.

It must run on Linux.

It must be compilable and executable on Arch Linux and Alpine Linux.

It must be compilable on glibc and musl based systems.

It must run in Xorg.

It must have a GUI.

It must have feature parity with the original version.

For each feature in the feature set determine:
- If it is a critical feature for normal functioning
- Which features depend on this feature
- Which features this feature depends on

Feature set for the build must be configurable with "make menuconfig".

Must be buildable with "make".

After identifying the full-parity feature set, create a plan for version 0.1 that includes only the critical features and port those features.

Features that have not yet been ported must appear in "make menuconfig" as greyed out or unselectable until they have been ported and are actually buildable.

Use git for source control.

For every feature ported:
- Add and commit with descriptive commit immediately after porting.

Work must be done in small, manageable stages.

Generate a plan for each stage before implementing it.

Keep a log detailing the progress of the porting process and update it for each feature and indicate if the plan for the feature has been written, if the feature has been implemented, etc.

The latest version of the original version of SillyTavern has already been cloned into the `@SillyTavern/` directory inside this project directory.

Create the `@SillyTavern_port/` directory inside this project directory if it doesn't exist.

The port should be contained within the `@SillyTavern_port/` directory inside this project directory.

Create the `@docs/plans/` directory inside this project directory if it doesn't exist.

Plans should be saved to the `@docs/plans/` directory inside this project directory.

Do not delete plans after implementing them.
