# GFOpenBrowser Lightweight Path

Status: design foundation, 7 October 2026

## Why this exists

The current project proves both ends of the spectrum:

- the existing GadTools GFOpenBrowser executable is about 49 KB in the current build tree;
- the full WebKit-based `GFOpenBrowser.engine` is about 102 MB.

The full engine is valuable: it provides the real DOM, modern CSS, JavaScriptCore and modern web behaviour.

But a large class of pages, local documentation, help systems, manuals, simple sites, generated status pages and appliance UIs do not need 102 MB of WebCore/JSC to become useful.

The lightweight path fills that gap.

## Architecture

```text
GFOpenBrowser GadTools shell
        |
existing URL/history/network layer
        |
HTML Lite parser
        |
small CSS resolver
        |
GFOpenLayout semantic tree
        |
GFOpenLayout display list
        |
pixman.library
        |
GFOpenRTG / AGA presentation
```

Images are decoded through Amiga datatypes and passed to GFOpenLayout as image handles.

The existing ARexx port remains `AMICRADLE.BROWSER`.

## What it is

A fast, small browser for content-first pages.

Initial HTML scope:

- html, head, title, body;
- headings and paragraphs;
- div/span;
- links;
- br/hr;
- strong/em/b/i/u/code/pre;
- ul/ol/li;
- blockquote;
- images;
- simple tables once GFOpenLayout tables land.

Initial CSS scope:

- type, class and ID selectors;
- inherited text properties;
- font family/size/style/weight;
- colour/background;
- margins/padding;
- width constraints;
- text alignment;
- simple borders;
- display block/inline/none.

That is enough to make a very large amount of documentation and classic/static web content look like a page rather than a text dump.

## What it is not

The lightweight path deliberately does not grow into another WebKit.

It does not initially provide:

- JavaScript;
- DOM mutation;
- modules;
- fetch or WebSockets;
- Canvas;
- WebGL/WebGPU;
- Grid or Flexbox;
- audio/video;
- service workers;
- the modern browser security/platform model.

Pages requiring those features belong to the full GFOpenBrowser engine.

## Escalation to the full browser

The shell can recognise capability pressure — for example scripts or unsupported layout features — and expose an `Open in Full Browser` action.

The two engines therefore complement each other:

- lightweight engine: starts quickly, small memory footprint, ideal for static/content pages;
- full engine: standards-heavy, JavaScript-capable pages.

They share URL/history/UI conventions instead of pretending to be unrelated applications.

## Reuse from today's GFOpenBrowser

Do not rewrite things that already work.

Reuse:

- GadTools browser shell;
- Back/Forward/Reload/Stop;
- URL handling;
- history;
- ARexx `OPENURL`;
- HTTP/HTTPS layer;
- cookies/cache only where the light engine actually needs them;
- datatypes image policy.

Replace the current HTML-to-text listview with a GFOpenLayout viewport.

## Text

GFOpenLayout owns line geometry, not font rasterisation.

The browser supplies a text provider. The preferred high-quality path is FreeType/HarfBuzz from the GFOpenAmiga stack; a system-font fallback can remain available for minimal installations.

UTF-8 must be converted/shaped deliberately. Raw UTF-8 bytes are never sent to Amiga bitmap-font drawing calls.

## Rendering

The lightweight engine should not introduce Cairo merely to draw a web page.

GFOpenLayout emits a small display list. A renderer translates its fill, text, rule and image operations to pixman.library and the display surface.

The first Pixman measurements already show the right optimisation targets: generic alpha OVER and A8-mask OVER are much more expensive than fill/copy. Text/glyph composition therefore gets priority as the browser renderer is built.

## Headless and automation

The same page can run without a window:

```text
HTML -> semantic tree -> layout -> display list
```

Tests can inspect stable semantic IDs, role/name, bounds, links and output hashes.

This is the lightweight browser's automation interface; it does not need a DOM-only testing API.

## Provisional engineering budget

These are budgets, not current claims:

- GFOpenLayout core: keep below 64 KiB m68k unless a measured need justifies growth;
- lightweight layout/HTML/CSS engine: target below 512 KiB before shared network/font/system libraries;
- ordinary content page: target single-digit MiB heap;
- no per-tab 100 MB engine image.

The first GFOpenLayout 0.1 m68k static library is already well inside that budget.

## Build sequence

1. GFOpenLayout L0: stable IDs, flow layout, display list and tests.
2. GFOpenWrite adapter: prove shared geometry against a real shipping application.
3. HTML Lite parser/CSS adapter.
4. Pixman display-list renderer.
5. Replace GFOpenBrowser Lite's text listview with the GFOpenLayout viewport.
6. Add link hit-testing, selection, scrolling and images.
7. Add simple tables/forms only from real page requirements.
8. Add `Open in Full Browser` escalation.

The target is not "a tiny standards benchmark browser". It is a very small browser that is genuinely pleasant and useful on an Amiga.
