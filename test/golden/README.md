# Golden tests

Caption image goldens run through the desktop renderer's offscreen SDL GPU path in
`test/antumbra/desktop_ui/` (target `desktop_ui_golden_test`, CTest label
`golden`). The renderer uses the bundled Inter Regular font at
`antumbra/desktop_ui/fonts/Inter-Regular.ttf`; its SIL Open Font License is next
to the font. The viewport, media time and cue data are fixed by each test.

Images are RGB PNG files. A pixel counts as different when any channel differs
by more than 24. A frame fails when more than 0.4 per cent of its pixels exceed
that threshold or when the mean channel difference exceeds 0.5. On failure the
test writes the rendered image and a red-on-black difference image under
`build/debug/test/antumbra/desktop_ui/actual/`. The CI build job uploads these
files with its reports.

To update a reference, first inspect the rendered output from a failed test.
Then run the focused case with `ALUMBRA_UI_GOLDEN_UPDATE=1`, review the changed
PNG, and run the complete `golden` label. Do not update references to make a
regression pass. The conformance manifest records each text format, source and
style-key coverage.

The desktop video output's golden-frame tests are in `test/penumbra_desktop/`
(target `penumbra_desktop_golden_test`, CTest label `golden`). Their references
are rendered by an independent CPU reference in the test, not committed images;
see [desktop video output](../../docs/architecture/desktop-video.md#tests).
