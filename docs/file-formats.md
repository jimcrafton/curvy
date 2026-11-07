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

Source: `include/curvy/io/svg/sketchsvg.h`, `src/io/svg/sketchsvg.cpp`. Curvy files are ordinary SVG plus `data-*` attributes carrying what SVG cannot (object type and name, frame angle, original color space, ppi, artboards, layers, guides, swatches, symbols, patterns, brushes, graphic styles, node graph).

- **Own files:** Full. Everything in the document model round-trips.
- **Other SVG files, supported:** `path` (all commands, arcs), `rect`, `circle`, `ellipse`, `line`, `polyline`, `polygon`, `g`, `transform`, fill/stroke/opacity as attributes or `style=""`, named / `#rgb` / `#rrggbb` / `rgb()` colors, linear and radial gradients (objectBoundingBox or userSpaceOnUse, `href` stops).
- **CSS:** rules in `<style>` elements with a simple selector (`.class`, `#id`, `tag`, `tag.class`, comma lists) are folded into each matching element's `style=""` before reading, in file order; the element's own `style=""` wins. Drawing tools such as CorelDRAW write all their colors this way (`.fil0 {fill:#EFDAC4}`). Not read: descendant / child / attribute / pseudo selectors, `@media`, specificity.
- **Other SVG files, skipped:** `<text>` not written by Curvy, `<image>`, clipping and masks, filters, `<use>` beyond what Curvy writes, animation.
- **Estimate:** roughly 60-70% of what typical exported-from-a-drawing-tool SVG contains; much lower for SVG that leans on filters or clip paths.

## Background Open and Save

Source: `backgroundtask.h`, `sketchcontroller.cpp`, `loadingoverlay.h`, `workspacecontroller.cpp`.

In the app, Open, Import PDF, Save and Save As run on a worker thread (`SketchDocumentController::setBackgroundIo(true)`; headless runs and tests stay synchronous). The worker calls `Document::load` / `save` on the live document: there is no scratch copy.

- **Busy flag:** `SketchDocument::busy()` is `Loading` or `Saving` from the start until the worker's end is handled on the UI thread. Every file command, `runCommand` and the autosave refuse while it is set (`refuseWhileBusy`). The controller holds an outer `SketchDocument::Batch` so the document announces nothing until the worker is done (the announcement, and with it the live-shape sync, then runs on the UI thread).
- **UI during the work:** the root view's children are hidden (the canvas and panels paint the document), the frame window is disabled with `EnableWindow` (no mouse or keys reach the document), and a `LoadingOverlay` shows the task name and a ring with the percentage - or a turning arc while the importer cannot tell. Esc cancels (polled with `GetAsyncKeyState`, since a disabled window gets no keys).
- **Hand-over:** the worker posts its end with `RunLoop::post`; `finishBusy` then waits for the thread (a condition variable in `BackgroundTask::waitFor`), releases the batch and the busy flag, and runs the same completion as the synchronous path. `onModifiedChanged`, which `Document::load` / `save` fire on the worker, is marshalled to the UI thread (`SketchDocumentController::onUiThread`).
- **Progress and cancel:** `TaskProgress` (atomic fraction and cancel flag) is passed to the readers: the SVG reader reports elements read out of all elements, the PDF import reports pages, the SVG writer reports objects. `.skb` and gzip steps are indeterminate and not interruptible.
- **Limits:** a job over 10 minutes (`setBusyLimits`) is cancelled by the controller's 33 ms tick. If the worker still ignores the cancel after 20 s the UI is given back with an error and the document stays busy until the thread really ends.
- **Cancelled / failed:** a cancelled or timed-out Open leaves an empty untitled document (the old content was already replaced). A cancelled Save leaves the file on disk untouched: the text is built before the file is opened for writing.
- **Startup:** a file named on the command line is opened from `RunLoop::onStart` (`WorkspaceController::openStartupFile`), because the worker reports through the loop.
- **Not covered:** the exports (PDF, EPS, image) still run on the UI thread; recovery from an autosave is synchronous.

## Sketch Binary (`.skb`)

Source: `sketchbin.h`, `sketchbin.cpp`. Chunked little-endian binary (`SKB1`), gzip body by default, for documents with very many shapes. Unknown chunks are skipped so newer files stay readable. Versions 1-4 are read.

- **Coverage:** Full for the document model (page, objects, appearance, text, graphic styles, groups). Geometry is stored as float32 steps, so values match to float precision, not exactly.
- Corrupt or truncated files are rejected without changing the open document.

## PDF export (`.pdf`)

Source: `include/curvy/io/pdf/pdfexport.h`, `src/io/pdf/pdfexport.cpp`. PDF 1.4, one page per artboard.

- **Supported:** vector paths; RGB, CMYK and gray color; axial and radial gradients (with stitched stops); tiling pattern fills; dashes, caps, joins, miter limit; clipping masks; group and object opacity and blend modes; symbols; pictures (with soft mask); text as outlines; brush strokes and variable-width strokes as outlines.
- **Rasterised:** shadows, glows and blurs become a 2x picture placed in position.
- **Not supported:** live text (no fonts embedded, so text is not searchable), ICC profiles, spot colors, layers (OCG), PDF/A or PDF/X conformance, compression beyond Flate.
- **Estimate:** High. Visually faithful for nearly all Curvy documents; not suited to prepress workflows.

## PDF and Illustrator import (`.pdf`, `.ai`)

Source: `include/curvy/io/pdf/pdfimport.h`, `src/io/pdf/pdfimport.cpp`. File > Import PDF / Illustrator. One artboard per page, laid out left to right with a 40 pt gap. The result is an untitled, modified document.

- **Supported:**
  - File structure: objects found by scanning (works without a cross-reference table, after incremental updates, and on slightly damaged files); object streams; page tree with inherited media box, resources and `/Rotate`.
  - Filters: Flate (with PNG predictors), ASCIIHex, ASCII85; JPEG (`DCTDecode`) for images only.
  - Content: paths (`m l c v y h re`), painting (`f F f* S s B B* b b* n`), graphics state (`q Q cm w J j M d gs`, with `LW LC LJ ML D ca CA BM`), form XObjects (nested, with matrix), images (8-bit gray, RGB, CMYK; JPEG; image masks; soft masks of the same size). Images keep the `ca` opacity they are drawn under.
  - Blend modes: `/BM` Multiply, Screen, Overlay, Darken, Lighten, ColorDodge, ColorBurn, HardLight, SoftLight, Difference and Exclusion become the object's blend mode. A form with `/Group /S /Transparency` drawn under `ca` below 1 or a blend mode becomes a group object that carries that opacity and blend mode; inside the form they start over (as the PDF model says). Isolated / knockout flags and a soft mask on a group are not honored; `Luminosity` and the other non-separable modes are not available.
  - JPEG images are decoded on several threads before the pages are read (`decodeJpegsInParallel`); the 90 MB, 103-page magazine imports in about 2 s.
  - Color: DeviceGray / RGB / CMYK, CalGray / CalRGB, ICCBased (by component count); Separation, DeviceN, Lab and Indexed are approximated (tints become gray, others are guessed by component count).
  - Gradients: shading patterns of type 2 (axial) and 3 (radial) with function types 2 and 3 (sampled at 17 points plus stop boundaries); the `sh` operator fills the current clip with the same gradient.
  - Clipping: `W` / `W*` become clipping groups (one per nested clip level, closed by `Q` or the end of the form); the clip path is the group's topmost member. A clip to the page's own bounds is dropped. `W*` makes the clip path even-odd.
  - Pages: a file of several pages is laid out side by side, so each page's content is wrapped in one clipping group of its page box (what a viewer crops would otherwise show on the neighboring page). A one-page file gets no such group.
  - Even-odd fills: `f*`, `B*` and `b*` set the object's fill rule, which is kept in SVG as `fill-rule="evenodd"`, written back as `f*` in PDF and `eofill` in EPS, and used by hit testing.
  - Soft masks: an ExtGState `/SMask` (luminosity or alpha, with `/BC`) is rendered over the page; every object painted while it is active is drawn through it and kept as a picture, because the document has no opacity masks. Such objects stop being editable vectors.
  - Embedded fonts (`src/io/pdf/fontprogram.cpp`, `FontProgram`): TrueType (`glyf`, composites included), OpenType with CFF outlines, Type 1 (`FontFile`: PFB or bare, eexec binary or hex, charstring and `Subrs` decryption, flex, hint replacement, `seac` accents, the font's own `/Encoding`), and bare CFF (`Type1C`, `CIDFontType0C`, with local and global subroutines, flex operators, CID charsets and FDSelect). A run of text in such a font becomes one path made of the font's own glyph outlines (named after its text), so the letterforms are exact whatever is installed; `PdfImportOptions::textAsOutlines = false` asks for live text instead. Glyphs are found by `/Differences` glyph name, then by Unicode through the font's cmap or glyph names, then by the font's own encoding; composite fonts go by CID (or `CIDToGIDMap`). Fonts are read once per file and their glyph paths cached. Not read: hinting, CFF `seac`-style accented endchar, Type 3 glyph procedures. The words are no longer editable text; where the characters are known the path's name keeps them.
  - Live text: each show operation (`Tj TJ ' "`) of a font whose program is missing or unreadable becomes a live Text object, with `BT Tf Td TD Tm T* Tc Tw Tz TL Ts Tr` honored (render modes fill, stroke, both; invisible is skipped). Characters come from the font's `ToUnicode` map, else its encoding (WinAnsi or MacRoman, plus `/Differences` glyph names); advances come from `/Widths` (`/W`, `/DW` for composite fonts), or from the installed font when the file gives none. The font is the installed one of the family in `BaseFont` (subset prefix and `MT`/`PS` suffixes dropped; Helvetica, Times and Courier map to Arial, Times New Roman and Courier New), bold and italic from the name and the descriptor. A `TJ` kern wider than a quarter em becomes a space. The run's length in the file is kept: the object's tracking is set so the stand-in font fills the same width (within 0.3 em per character). A font that is not installed (and has no readable program) is replaced by Courier New (fixed-pitch descriptor), Times New Roman (serif) or Arial, and widths can drift where the file gives none (checked against PyMuPDF's render on a 115-page Courier screenplay and a 103-page magazine with Type1 and Type0 fonts, 90 MB, imported in about 4 s); that magazine is encrypted and opens as it is; composite fonts without `ToUnicode` and Type 3 fonts are skipped.
- **Not supported (skipped, not an error):** mesh shadings (types 4-7), function types 0 and 4, tiling patterns, non-separable blend modes (Luminosity ...), soft-mask transfer functions, optional content (layers), inline images, 1/2/4/16-bit images, JPEG 2000 / CCITT / JBIG2 / LZW filters, AES-256 encryption (revision 5 / 6) and files that need a password (both refused).
  - Encryption: the standard security handler with an empty user password opens as is: RC4 40 / 128 bit and AES-128 (`/V` 1 to 4, `/R` 2 to 4), with crypt filters (`StmF`, `StrF`, `Identity`), per-object keys, `EncryptMetadata`, and the file key checked against the user entry (so a file that really wants a password is refused, not shown as garbage). Strings are decrypted when the file is read, streams when they are used. `src/pdfcrypt.cpp` holds MD5, RC4 and AES (checked against the published test vectors); decrypting the 90 MB magazine adds about 0.8 s. The Open dialog only says "Could not open" for a refused file; `documentFromPdf` can return the reason.
- **Illustrator `.ai`:** only files saved with "Create PDF Compatible File" (all modern ones) work. The private Illustrator data is ignored, so layers, live effects, symbols and brushes arrive as plain geometry. Pre-CS PostScript-based `.ai` files are not supported.
- **Estimate:** Partial. Good for logos, icons and flat or gradient artwork. Poor for documents that are mostly text. Checked on three real Illustrator exports (`DarkTech_*`), which import with clipping and gradient shading intact.

## EPS export (`.eps`)

Source: `include/curvy/io/eps/epsexport.h`, `src/io/eps/epsexport.cpp`. Encapsulated PostScript level 2 for the **active artboard** only.

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
