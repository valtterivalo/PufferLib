# RLVG Inferno Paper Review Notes v5

Status: final format and double-blind mechanics pass for `paper_v0.tex` and `paper_v0.pdf`.

## External Critique Pass

ChatGPT reviewed the pushed GitHub state at commit `b0b517b4a4cc74d66153b00718ba0eeb565adbba` with the prompt restricted to submission mechanics and double-blind risk.

Main feedback:

- No fatal blockers were found.
- The TeX is anonymous: `Anonymous Authors`, no date, no GitHub URL, no repo path, and no personal name in the author block.
- The main nonfatal risk is soft attribution through the paper's PufferLib specificity. This is not an explicit double-blind violation, but reviewers familiar with the ecosystem may infer authorship.
- Plain `article` format is acceptable only if the CFP does not require a template. The local CFP and visible OpenReview venue metadata did not show a separate style-file requirement.
- The only suggested mechanics cleanup was to strip metadata from the replay figure and rebuild the PDF.

## Local Check

- `exiftool` is not installed on this machine.
- The replay figure was already rebuilt with ImageMagick `-strip`.
- Raw string scans on `inferno_replay_wave69.jpg` find no EXIF, XMP, Photoshop, date, software, artist, copyright, personal name, local path, or repo path strings.
- Raw string scans on `paper_v0.pdf` find no date metadata, local paths, W&B ids, run ids, personal names, or repo paths.

## Decision

No paper content changes are needed from this pass.
