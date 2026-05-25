# RLVG Inferno Layout Audit v0

Date: 2026-05-25.

Artifact: `papers/rlvg-inferno/paper_v0.pdf`.

## Checks

- `pdflatex` ran twice with no warnings found by the log scan.
- `pdfinfo` reports 4 pages, letter size, no author metadata, no creation date, no modification date, no JavaScript, no encryption, and PDF 1.7.
- `pdftotext -layout` shows the title, abstract, five sections, three tables, one figure caption, and references in readable order.
- Table 3 fits on page 3 after the Zuk HP label fix.
- References fit on page 4.
- `pdftocairo` rendered all four pages at 1190 by 1540 pixels.
- `pdfimages -list` reports one embedded JPEG image on page 2 at 1280 by 800 pixels.

## Rendered Page Sizes

- Page 1: 397985 bytes
- Page 2: 731438 bytes
- Page 3: 389842 bytes
- Page 4: 326764 bytes

## Conclusion

The rendered PDF is visually and structurally stable for submission.
