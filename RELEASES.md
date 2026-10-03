# Publishing downloadable releases

Users should download a ready-to-run executable from GitHub Releases; they are not expected to compile the project.

## Before publishing

The original project code is MIT-licensed; include the [LICENSE](LICENSE) and [third-party notice](THIRD_PARTY_NOTICES.md) with the release. The InpOut MIT notice is also included in the source tree at `resources/inpout-license.txt`. Include the notices alongside the downloadable executable (for example, bundle them in the release ZIP, or attach the notice file as a release asset).

For each release:

1. Build from the source using `./build.ps1` on a clean Windows x64 machine.
2. Confirm the hardware-free tests pass, then perform the intended hardware validation for that version.
3. Smoke-test the Release executable on a supported Windows/RDNA 4 system. Confirm startup, readback, reset/undo, ADLX controls, SMU controls, and fan behavior as applicable. The automated suite alone is not hardware validation.
4. Copy `build-native/RDNA4OCPlus.exe` to a release staging location and name it `RDNA4OCPlus-vX.Y.Z-x64.exe`, replacing `X.Y.Z` with the release version.
5. Calculate and record the SHA-256 of the final executable.
6. Create a GitHub Release for the matching version tag, attach the `.exe` asset (and optionally a `.sha256` checksum file), and include concise release notes with supported hardware/driver, known limitations, and safety caveats.
7. Verify the published asset can be downloaded and that its checksum matches before linking the release from the README.

Do not commit the executable into the source tree or Git history. GitHub Releases provides a direct download while keeping clones of the repository small and the source history clean.
