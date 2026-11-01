# Publication checklist

What to check before this repository, or a binary built from it, is published, with the results of the checks run on
2026-10-18 on branch `legal` (cut from `main` at `05d8d68`). Counts only: no private value is written here or was
printed while checking. Nothing was removed, untracked or rewritten; each open item is the owner's decision. Every check
below is one command now, `python3 tools/publication-check.py all` (section 8), which ends in a verdict.

Related: [`LEGAL.md`](../LEGAL.md) (what the project is and is not), [`LICENSING.md`](../LICENSING.md),
[`NOTICE.md`](../NOTICE.md), [`PROVENANCE.md`](PROVENANCE.md) (where the platform knowledge comes from).

## Summary

| Check | Result on 2026-10-18 | Open for the owner |
| --- | --- | --- |
| Private values in the tracked tree | None found | No |
| Private values in history | This PC's home path in 10 commits; two LAN addresses on branch `browser` | Decided: history kept; `browser` switches to `192.0.2.x` before it merges |
| Tracked files under `klog/`, `.claude/`, `evidence/` | 0, 0, 6151 | Decided: `evidence/` published as it is |
| Game assets, firmware, keys, disc images | None found | No |
| Licence texts staged next to the runtime | Staged for every component (after this check) | Decided: the three closest texts accepted; LICENSING.md signed off |
| Microsoft logo | Removed on this branch (Start button, prototype) | No |
| Microsoft names and texts in the prototype | Removed on this branch; product names kept | No |
| Neutral wording (README, architecture, features) | Done on this branch | Other docs, if wanted |

## 1. Private values in the tracked tree

Every tracked file of the working tree (gzip members unpacked) was searched for each category below; the console's
address and the FTP credentials were taken from the ignored `.env` files and matched exactly, this PC's own LAN
addresses from `ip addr`, and personal e-mail addresses through `tools/privacy_check.py`'s hash list.

| Category | Hits |
| --- | --- |
| The console's address (from `.env`) | 0 |
| This PC's LAN addresses | 0 |
| Other private LAN addresses (10/8, 172.16/12, 192.168/16, 100.64/10) | a made-up fixture in `tests/test_console_guard.py` only |
| FTP user or password (from `.env`) | 0 |
| The owner's personal or work e-mail (blocked list) | 0 |
| Other e-mail addresses | upstream authors in vendored files and patches (Wine, GE-Proton, GLM, volk, GStreamer) and example domains |
| GitHub no-reply addresses | 91 (commit identities in patch headers and the privacy test) |
| Access tokens, API keys, private keys | 0 |
| MAC addresses, serial numbers, console ids | 0 |
| This PC's home path | 0 (one `/home/<other user>/` example in a test fixture) |
| `C:\Users\<name>` | `steamuser` (Proton's, in older prefixes), `public`, and `Mihawk` (the prototype's demo user, and the Windows user since v3.0.0-alpha-3) |

## 2. Private values in history

`main` (375 commits) and all local refs (424 commits) were searched the same way, added lines, authors and messages;
`python3 tools/privacy_check.py scan --all` passes.

| Category | `main` | All refs |
| --- | --- | --- |
| The console's address, this PC's LAN addresses, FTP credentials | 0 | 0 |
| The owner's personal or work e-mail | 0 | 0 |
| Other private LAN addresses | 0 (beside the test fixture) | 7, on branch `browser` (below) |
| Tokens and keys | 0 (one false positive in GLM's vendored jQuery) | 0 (same) |
| MAC addresses, serial numbers, console ids | 0 | 0 |
| This PC's home path | 302 occurrences, added or removed in 10 commits | the same |

The home path (`/home/` and the local user name, which is the same as the public handle) came in with deployment
records in `evidence/payload-graphics-*-deployment.json` (`7561901`, merged in `dc96d53`), two archived notes under
`docs/history/2026-10-07/` (`6aa3d62`), the debug paths inside the removed helper ELF (`5d37298`, `991911a`, removed in
`a138409`), and vendored files of `f1b2993`; the commits that touched it are `f1b2993`, `5d37298`, `19215d8`, `a138409`,
`39d4827`, `ee6f8f6`, `7561901`, `6aa3d62`, `5d4bf1e` and `4552f7e`. Today's tree has none.

- [x] **Owner:** keep that history as it is, or rewrite it before publishing (a rewrite changes every later hash).
  Decided 2026-10-18: kept as it is (the user name is the public handle; `main` is already on GitHub up to `40fd0dd`).
  Publication checks of history run with `--allow 'home path'`.

Branch `browser` (`a6a2b1e`, not on `main`) adds `192.168.x.20` with port 1749 and `192.168.x.2` to the browser
policy's tests, comments and prototype (`src/pw_browser_policy.*`, `tests/test_pw_browser_policy.c`,
`prototypes/windows11-shell/js/apps/browser.js`). Port 1749 is the TLS fixture this PC serves, so the first may be a
former LAN address of this PC; neither is the console's or this PC's address today.

- [ ] **Owner:** before `browser` is merged or published, replace those two with documentation addresses
  (`192.0.2.x`). Decided 2026-10-18: yes; the browser session makes the change, gated, before `browser` merges.

## 3. Tracked files under `klog/`, `.claude/` and `evidence/`

| Folder | Tracked | Ignored by `.gitignore` | Recommendation |
| --- | --- | --- | --- |
| `klog/` | 0 files | yes | nothing to do |
| `.claude/` | 0 files | yes | nothing to do |
| `evidence/` | 6151 JSON files, 13.8 MB | no | owner's choice (below) |
| `.impeccable/` | 1 file (a design-tool note) | no | keep, or ignore with the design tool's state |

`evidence/` is committed by design (AGENTS.md: measurements live in `evidence/`). Its files hold no account names,
launcher user ids, tokens or e-mail addresses. 4472 of them named the program a run started: the project's probes,
Wine's tests, and the games and launcher clients tested, always under `C:\Games`, `C:\Program Files (x86)`, `C:\windows` or `Z:\data`; no
personal folder.

- [x] **Owner:** publish `evidence/` as it is, or untrack it (`git rm -r --cached evidence` and an ignore line) and
  publish a curated subset. Decided 2026-10-18: published as it is.

## 4. Game assets, firmware, keys and disc images

None. Every file was checked by extension and by magic bytes (ELF, PE, PS4 and PS5 SELF, PUP, PKG, ISO 9660,
zip, 7z, rar, xz, PEM). The binary files tracked are fonts (Selawik, Cascadia Mono), the title's icon and the
template's sample screenshots (`ps5/screenshots/`), six SPIR-V shaders built from this repository's GLSL, the Wine
theme's resource container `themes/windows11/source/template.msstyles` (the only PE file, recorded in
[`NOTICE.md`](../NOTICE.md)) and its four tiny resource-name files, and gzip-compressed sprite data. `.gitignore`
already refuses `*.pkg`, `*.iso`, `*.sav`, `*.prx` and `*.sprx`.

- [x] No game data, firmware, keys or images.
- [ ] **Owner:** `template.msstyles` has no recorded upstream revision ([`NOTICE.md`](../NOTICE.md), "Written for this
  project"); record one if it can be found.

## 5. Licence texts staged next to the runtime

On 2026-10-18 every binary was found shipping without some of its texts (Wine, VKD3D-Proton, GnuTLS and Nettle,
FreeType, Mesa, Wine Mono, the CA bundle, ICU, the mingw-w64 runtime, the Lapy daemon and the title's vendored
libraries). `tools/stage-licences.py` now stages them: the runtime's `share/licenses/` beside what `tools/stage-wine.py`
already staged, and a `licenses/` folder in the title (`dist/PPSA99170`). Sources and pins are in
[`NOTICE.md`](../NOTICE.md), "Licence texts shipped with the binaries"; a missing or changed text stops the build, and
`tests/test_stage_licences.py` lists every expected file (52 in the runtime, 12 in the title) and checks a production
build's output against the list. Phase 4 of [`SELF_INSTALL.md`](SELF_INSTALL.md) added the fonts' texts (Liberation 1.x
for `arial.ttf`, `times.ttf` and `cour.ttf`, Apache-2.0 for `msyh.ttf`), Wine Mono's GPL, LGPL and MS-PL texts and the
source locations of Wine Mono and Wine Gecko.

- [x] Every shipped component has its licence text beside it.
- [x] **Owner:** three texts are the closest available, not the exact build input, and say so in `NOTICE.md`: ICU's
  (the GE-Proton archive does not record its ICU 68 revision; `release-68-2` is used), the mingw-w64 runtime's (the
  toolchain's runtime is a 15.0.0-alpha snapshot; `v14.0.0`'s notice is used) and Mesa's single licence page for both
  of its pinned revisions (they are the same file today).
  Decided 2026-10-18: accepted as they are, with `NOTICE.md` saying so.
- [x] **Owner:** [`LICENSING.md`](../LICENSING.md) as `bbbf3c6` left it (the Mihawk mark under LGPL-2.1-or-later,
  non-affiliation with Shueisha and Toei Animation added): signed off 2026-10-18.

## 6. Trademarks and Microsoft material

- [x] **The Windows logo is gone** on this branch: the taskbar's Start button draws the project's Mihawk mark at every
  DPI (Wine patch 1162, `src/pw_taskbar_logo.h`), the captured Start sprites are deleted, and the prototype's Start
  glyph, boot and shut-down logo, favicon and consent shield no longer show the four panes.
- [x] [`LEGAL.md`](../LEGAL.md) states non-affiliation (Sony, Microsoft, Valve, Shueisha, Toei Animation), the
  trademark list, and that no proprietary Microsoft code, icons, wallpapers, sounds, fonts or logo are used. The
  Microsoft-authored open-source assets used are listed there and in [`NOTICE.md`](../NOTICE.md).
- [x] **Microsoft's name and texts are gone from the prototype** (the owner's decision of 2026-10-18, reversing
  `caacf1e`'s): Terminal's banners no longer reproduce Windows PowerShell's and Command Prompt's copyright lines or link
  aka.ms, Task Manager and the consent prompts name "Mihawk OS" as the publisher, the shut-down dialog is "Shut Down
  Mihawk OS", and Settings' clipboard and device-usage texts no longer mention a Microsoft account or Microsoft
  experiences (`js/apps/small.js`, `js/apps/taskmgr.js`, `js/dialogs.js`, `js/taskbar.js`, `js/session.js`,
  `js/apps/settings.js`). Product names stay names ("Windows Explorer", "Windows PowerShell" and PowerShell's own error
  identifiers), as the owner's wording rule keeps them.
- [x] **Owner:** the reference captures the shell scorecard compares with (`.impeccable/reference-1920` in the design
  worktree, made from `4e52ee1`) still show the old Start logo; re-capture them from this prototype.
  Done 2026-10-18 from `bbbf3c6`: all 172 states. 71 differed beyond the Start button. Reviewers judged 59 of them the
  legal change and 2 noise. In 7, the old set was wrong: a stray focus ring, tooltips its own "how" excludes, and a
  dimmed screen. 2 are timer-driven progress values. The old set is kept as `reference-1920-4e52ee1/`, and the scripts
  as `.impeccable/reference-tools/`. Batch 4's captures re-scored against it:
  `evidence/shell-scorecard-2026-10-18-batch4-01-reference-bbbf3c6.json`, 25.3% (was 24.8%).

## 7. Wording

- [x] README.md: the jailbreak and payloads are stated as what the tested setup needs and the user supplies; "dumps"
  and "backup" are no longer used as a use; the exploit chain's entries keep their names and versions but no links;
  no link to a piracy site was found. docs/ARCHITECTURE.md and docs/FEATURES.md needed no change.
- [ ] **Owner, if wanted:** seven other documents still say "dump" or "dumps" (`docs/IA32_DESIGN.md`,
  `skills/proton-agent-native/SKILL.md`, `docs/PLAN.md`, `docs/MMU4K.md`,
  `docs/CONSOLE_QUEUE.md` and two under `docs/history/`), mostly for crash and memory dumps; the tools' option and
  message texts (`tools/console-setup.py`) too.

## 7a. Questions for a lawyer

Before a public release, from this checklist, [`PROVENANCE.md`](PROVENANCE.md) and
[`SELF_INSTALL.md`](SELF_INSTALL.md) (phase 9):
- the Lapy daemon (shipped in the title folder) under anti-circumvention law;
- how the platform knowledge was obtained, including the static reading of the owner's own 10.01 firmware copy
  (PROVENANCE.md, "Local firmware reference"; its open questions were closed on 2026-10-19);
- codec patents in FFmpeg (the full set, or decoders only);
- the "Proton" names left (the `Proton UI` font, `/data/ps5proton`, the `PS5PROTON` content id suffix, `UP9000`), and
  "Mihawk";
- the Windows 11 look and the "Mihawk 11" wordmark;
- whether links are enough for corresponding source.

## 8. Before each publication

```sh
python3 tools/publication-check.py all          # the tree, and every local ref's history: 0 clean, 1 to fix
python3 tools/publication-check.py history main # only what a push of main would carry
```

It checks everything in sections 1 to 5: the `.env`'s console address and FTP credentials, this PC's LAN addresses,
other private LAN addresses, blocked personal e-mail addresses (`tools/privacy_check.py`), tokens and keys, MAC
addresses, serial numbers and console ids, the build user's home path and Windows user folders, in the tree and in
history; and in the tree, tracked files under `klog/` or `.claude/`, files that could hold game data, firmware or keys,
source files without an SPDX line, the licence texts of a production build (a title or runtime without its licences
folder fails), and what a build must never carry: a wallpaper other than the default, Bubble City by Skeuoss (`proton.bmp`,
stamped from `themes/windows11/wallpapers/skeuoss-bubble-city.jpg`; its thumbnail is compared with the picture's: the
build's is 0.4 levels away, Bubble Earth 68.5, Aqua World 54.7, Glass City 75.5, Aero Meadow 72.8), a native Microsoft binary (a PE without Wine's builtin
marker that names Microsoft Corporation), and the probe prefix or test programs. Every build, the owner's own and a
public one alike, ships a wallpaper it may redistribute: Aero Meadow from 2026-10-18, when it replaced Windows 11's Bloom,
Bubble Earth from 2026-10-25, and Bubble City by Skeuoss, used with the artist's permission, from 2026-10-31. It prints counts and places, never a
value; known false positives are listed in the tool and reported as allowed. `--allow '<category>'` accepts a finding
the owner has decided to keep (for example `--allow 'home path'` while the history in section 2 stays). Its host test
plants each category in a scratch repository and checks it is caught and not printed.

Its first run, 2026-10-18 on `legal`: the tree is clean (8921 files); `main`'s history (376 commits) fails only on
the home path of section 2; all local refs (427 commits) also on `browser`'s two addresses. Every source file the
project wrote carries an SPDX line except LICENSING.md's named exceptions; `tools/gcprobe.c` lacked one and has it
now.

- [ ] `python3 tools/publication-check.py all` ends `verdict: clean`, or every remaining category is decided above.
- [ ] The tree names no game, launcher or emulator experiment: `python3 tools/publication-check.py tree` (category
  'named game or launcher'; only the trademark notices and upstream Wine identifiers are listed as allowed).
- [ ] The open items above are decided.
