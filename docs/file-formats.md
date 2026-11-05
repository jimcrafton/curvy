# File formats

What Curvy reads and writes, and how much of each format is supported.

Coverage ratings are judgements, not measurements: **Full** means everything the format normally holds for vector illustration round-trips; **High** means common content works and only rare features are missing; **Partial** means a useful subset; **Minimal** means only the basics. Anything not listed under "Supported" should be assumed unsupported.

## Summary

| Format | Extension | Open | Save / Export | Coverage |
|---|---|---|---|---|
| SVG | `.svg` | yes | yes | Native format. Own files: Full. Foreign files: Partial |
| Compressed SVG | `.svgz` | yes | yes | Same as SVG (gzip detected by content) |
| Sketch Binary | `.skb` | yes | yes | Full (native, lossless for the document model) |
| PDF | `.pdf` | import | export | Export: High. Import: Partial |
| Illustrator | `.ai` | import | no | Partial, only for PDF-compatible files |
| EPS | `.eps` | no | export | Partial |
| PNG / BMP / QOI | `.png` `.bmp` `.qoi` | place / trace | export | PNG: Full. BMP, QOI: Full for opaque or alpha pixels |
| JPEG | `.jpg` `.jpeg` | place / trace | no | Read only (Blend2D cannot write JPEG) |

Formats are told apart by their first bytes, not the file name (`.skb`, gzip, `%PDF-`).

## SVG (`.svg`, `.svgz`) - the native document format

Source: `sketchsvg.h`, `sketchsvg.cpp`. Curvy files are ordinary SVG plus `data-*` attributes carrying what SVG cannot (object type and name, frame angle, original color space, ppi, artboards, layers, guides, swatches, symbols, patterns, brushes, graphic styles, node graph).

- **Own files:** Full. Everything in the document model round-trips.
- **Other SVG files, supported:** `path` (all commands, arcs), `rect`, `circle`, `ellipse`, `line`, `polyline`, `polygon`, `g`, `transform`, fill/stroke/opacity as attributes or `style=""`, named / `#rgb` / `#rrggbb` / `rgb()` colors, linear and radial gradients (objectBoundingBox or userSpaceOnUse, `href` stops).
- **Other SVG files, skipped:** `<text>` not written by Curvy, `<image>`, clipping and masks, filters, CSS stylesheets (`<style>`), `<use>` beyond what Curvy writes, animation.
- **Estimate:** roughly 60-70% of what typical exported-from-a-drawing-tool SVG contains; much lower for SVG that leans on CSS, filters or clip paths.

## Sketch Binary (`.skb`)

Source: `sketchbin.h`, `sketchbin.cpp`. Chunked little-endian binary (`SKB1`), gzip body by default, for documents with very many shapes. Unknown chunks are skipped so newer files stay readable. Versions 1-4 are read.

- **Coverage:** Full for the document model (page, objects, appearance, text, graphic styles, groups). Geometry is stored as float32 steps, so values match to float precision, not exactly.
- Corrupt or truncated files are rejected without changing the open document.

## PDF export (`.pdf`)

Source: `pdfexport.h`, `pdfexport.cpp`. PDF 1.4, one page per artboard.

- **Supported:** vector paths; RGB, CMYK and gray color; axial and radial gradients (with stitched stops); tiling pattern fills; dashes, caps, joins, miter limit; clipping masks; group and object opacity and blend modes; symbols; pictures (with soft mask); text as outlines; brush strokes and variable-width strokes as outlines.
- **Rasterised:** shadows, glows and blurs become a 2x picture placed in position.
- **Not supported:** live text (no fonts embedded, so text is not searchable), ICC profiles, spot colors, layers (OCG), PDF/A or PDF/X conformance, compression beyond Flate.
- **Estimate:** High. Visually faithful for nearly all Curvy documents; not suited to prepress workflows.

## PDF and Illustrator import (`.pdf`, `.ai`)

Source: `pdfimport.h`, `pdfimport.cpp`. File > Import PDF / Illustrator. One artboard per page, laid out left to right with a 40 pt gap. The result is an untitled, modified document.

- **Supported:**
  - File structure: objects found by scanning (works without a cross-reference table, after incremental updates, and on slightly damaged files); object streams; page tree with inherited media box, resources and `/Rotate`.
  - Filters: Flate (with PNG predictors), ASCIIHex, ASCII85; JPEG (`DCTDecode`) for images only.
  - Content: paths (`m l c v y h re`), painting (`f F f* S s B B* b b* n`), graphics state (`q Q cm w J j M d gs`, with `LW LC LJ ML D ca CA`), form XObjects (nested, with matrix), images (8-bit gray, RGB, CMYK; JPEG; image masks; soft masks of the same size).
  - Color: DeviceGray / RGB / CMYK, CalGray / CalRGB, ICCBased (by component count); Separation, DeviceN, Lab and Indexed are approximated (tints become gray, others are guessed by component count).
  - Gradients: shading patterns of type 2 (axial) and 3 (radial) with function types 2 and 3 (sampled at 17 points plus stop boundaries); the `sh` operator fills the current clip with the same gradient.
  - Clipping: `W` / `W*` become clipping groups (one per nested clip level, closed by `Q` or the end of the form); the clip path is the group's topmost member. A clip to the page's own bounds is dropped.
  - Soft masks: an ExtGState `/SMask` (luminosity or alpha, with `/BC`) is rendered over the page; every object painted while it is active is drawn through it and kept as a picture, because the document has no opacity masks. Such objects stop being editable vectors.
- **Not supported (skipped, not an error):** text (glyphs are not converted to outlines, so words disappear), the even-odd rule for `W*` clips (clips are non-zero; fills read `f*`, `B*` and `b*` as even-odd, and the setting is kept in SVG as `fill-rule="evenodd"` and written back as `f*` in PDF and `eofill` in EPS), mesh shadings (types 4-7), function types 0 and 4, tiling patterns, blend-mode graphics state, soft-mask transfer functions, transparency groups, optional content (layers), inline images, 1/2/4/16-bit images, JPEG 2000 / CCITT / JBIG2 / LZW filters, encrypted files (refused).
- **Illustrator `.ai`:** only files saved with "Create PDF Compatible File" (all modern ones) work. The private Illustrator data is ignored, so layers, live effects, symbols and brushes arrive as plain geometry. Pre-CS PostScript-based `.ai` files are not supported.
- **Estimate:** Partial. Good for logos, icons and flat or gradient artwork. Poor for documents that are mostly text. Checked on three real Illustrator exports (`DarkTech_*`), which import with clipping and gradient shading intact.

## EPS export (`.eps`)

Source: `epsexport.h`, `epsexport.cpp`. Encapsulated PostScript level 2 for the **active artboard** only.

- **Supported:** vector paths; RGB, CMYK and gray color; dashes, caps, joins; clipping groups; symbols; text as outlines; brush strokes as outlines; gradients as 96 vector bands; pictures as ASCIIHex RGB.
- **Approximated:** colors with opacity are mixed with the page color (white if the page is transparent). Gradient and picture transparency is flattened onto the page color. Objects with shadows, glows, blurs or pattern fills become opaque pictures.
- **Not supported:** transparency, group opacity, blend modes, multi-artboard output, preview image (TIFF/EPSI header), embedded fonts or live text, DSC color separation comments, EPS import.
- **Estimate:** Partial. Correct for flat artwork on a plain page; visibly different where transparency overlaps other content.

## Raster export and pictures

Source: `sketchexport.h`, `imagedata.h`.

- **Export:** File > Export As Image and Export All Artboards write `.png`, `.bmp` or `.qoi` at the document's resolution (pixels per inch / 72). BMP gets a white page when the document is transparent. There is no JPEG, WebP, TIFF or GIF export.
- **Place / trace:** `.png`, `.jpg`/`.jpeg`, `.bmp`, `.qoi` can be placed as image objects (stored as the original bytes) or traced to vectors (Trace Image accepts png, jpg, bmp). No WebP, TIFF, GIF, HEIC or RAW.

## Not supported

DXF, PSD, WebP, TIFF, GIF, EMF/WMF, CDR, Sketch/Figma files, and EPS or PostScript import.

## Tests

`tests/test_sketch_model.cpp`: `SketchPdf*` (export), `SketchEps*`, `SketchPdfImport*` (round trips through Curvy's own PDF output, a hand-written PDF with rotation, a form and dashes, and truncated-file robustness). PDF import has only been tested against Curvy's own exports and a small hand-written file, not against a corpus of real-world PDFs or Illustrator files, so real-world behaviour may be worse than the ratings above suggest.
