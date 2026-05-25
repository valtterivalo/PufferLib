# RLVG Inferno Morning Audit Checklist v0

Status: run at or after 09:00 Helsinki time before marking the overnight paper goal complete.

## Gate

- Confirm current time is at or after 09:00 Helsinki time.
- Confirm the branch is clean and pushed.
- Confirm the user has had a chance to inspect the bundle.

## Commands

```bash
cd /Users/valtterivalo/Projects/pufferlib-metal
date
pwd
git status --short --branch
```

```bash
shasum -a 256 \
  papers/rlvg-inferno/paper_v0.pdf \
  papers/rlvg-inferno/paper_v0.tex \
  papers/rlvg-inferno/figures/inferno_replay_wave69.jpg
```

Expected hashes:

- PDF: `5b48d068765a5af807223c99460d4a3f381acbd2f2aa688597731b8672105e9c`
- TeX: `eccb3d86709bcf181cfdc88b91eb90645711df132c298a11a3f0ebe137a51649`
- Replay figure: `4ef69b90fd5276f71e9f844f1a7e4ca7d0b45dcfad54459cfc4a5ca128567369`

```bash
pdfinfo papers/rlvg-inferno/paper_v0.pdf
pdfimages -list papers/rlvg-inferno/paper_v0.pdf
rm -rf /tmp/rlvg_cairo_preview
mkdir -p /tmp/rlvg_cairo_preview
pdftocairo -png -r 140 papers/rlvg-inferno/paper_v0.pdf /tmp/rlvg_cairo_preview/page
wc -c /tmp/rlvg_cairo_preview/page-*.png
```

Expected artifact shape:

- 4 pages.
- Letter page size.
- Blank author metadata.
- No JavaScript.
- No encryption.
- One embedded JPEG image on page 2.
- Four rendered PNG pages.

```bash
rg -n '[\x3b\x{2014}]' papers/rlvg-inferno/paper_v0.tex
pdftotext papers/rlvg-inferno/paper_v0.pdf - | rg -n '\?\?|TODO|FIXME|PLACEHOLDER|valtteri|valo|/users|wandb|whl5mxay|j6bgoiu4|openai|codex|puffertank'
strings papers/rlvg-inferno/paper_v0.pdf | rg -n -i 'creationdate|moddate|eest|valtteri|valo|/users|pufferlib-metal|wandb|whl5mxay|j6bgoiu4|openai|codex|puffertank'
strings papers/rlvg-inferno/figures/inferno_replay_wave69.jpg | rg -n -i 'exif|xmp|photoshop|date|software|artist|copyright|valtteri|valo|/users|pufferlib'
```

Expected scan result: no matches.

```bash
cc -std=c11 -O0 -g -I. -o /tmp/test_inferno_attack_styles ocean/osrs/tests/test_inferno_attack_styles.c -lm
/tmp/test_inferno_attack_styles
```

Expected contract test result: `1441/1441 tests passed`.

## Completion Rule

Only mark the goal complete after all checks above pass and the user has had a chance to inspect the bundle.
