import { readFileSync } from "node:fs";
import { defineConfig } from "vitepress";

// CeeSafe code is highlighted by the grammar of CeeSafe's VS Code extension (a copy: ceesafe.tmLanguage.json here,
// from CeeSafe's editors/vscode/syntaxes), so the site and the editor agree.
const ceesafe = JSON.parse(readFileSync(new URL("./ceesafe.tmLanguage.json", import.meta.url), "utf8"));

const guide = [
  {
    text: "Start",
    items: [
      { text: "What ClayWind is", link: "/guide/introduction" },
      { text: "Getting started", link: "/guide/getting-started" },
      { text: "A frame", link: "/guide/frame" },
    ],
  },
  {
    text: "Building a window",
    items: [
      { text: "Layout", link: "/guide/layout" },
      { text: "Styles", link: "/guide/styles" },
      { text: "Text", link: "/guide/text" },
      { text: "Controls", link: "/guide/controls" },
      { text: "Lists, grids and charts", link: "/guide/lists-grids-charts" },
      { text: "Editing text", link: "/guide/editing" },
      { text: "Input: keys, pointer, touch", link: "/guide/input" },
      { text: "Drawing: paths, images, effects", link: "/guide/drawing" },
    ],
  },
  {
    text: "An app",
    items: [
      { text: "State, actions and menus", link: "/guide/app" },
      { text: "A desktop host", link: "/guide/desktop" },
      { text: "Phones", link: "/guide/phones" },
      { text: "The web", link: "/guide/web" },
      { text: "Accessibility", link: "/guide/accessibility" },
      { text: "From C, Swift, Kotlin and Rust", link: "/guide/bindings" },
    ],
  },
  {
    text: "Tools",
    items: [
      { text: "Packing, hot reload, the inspector", link: "/guide/tools" },
      { text: "Testing an app", link: "/guide/testing" },
      { text: "Platforms and status", link: "/guide/status" },
    ],
  },
];

export default defineConfig({
  title: "ClayWind",
  description: "A Tailwind-flavoured immediate-mode UI toolkit in CeeSafe, for desktops, phones and the web.",
  base: "/Clay/",
  srcExclude: ["README.md"],
  cleanUrls: true,
  lastUpdated: true,
  markdown: {
    languages: [{ ...ceesafe, name: "ceesafe", aliases: ["CeeSafe", "cee"] }],
  },
  themeConfig: {
    nav: [
      { text: "Guide", link: "/guide/introduction" },
      { text: "CeeSafe", link: "https://ceesafe.github.io/CeeSafe/" },
    ],
    sidebar: { "/guide/": guide },
    socialLinks: [{ icon: "github", link: "https://github.com/CeeSafe/Clay" }],
    search: { provider: "local" },
    outline: [2, 3],
    footer: { message: "ClayWind is MIT licensed. Its layout engine is a port of Clay by Nic Barker (zlib)." },
  },
});
