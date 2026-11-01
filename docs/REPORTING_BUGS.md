# Reporting a bug

## 1. Your setup

- PS5 model (standard, Digital, Slim or Pro) and firmware (Settings, System, System Software).
- Your homebrew enabler and its version (kstuff or Kstuff Lite), and the versions of elfldr, websrv and
  ShadowMountPlus.
- The MihawkOS release (for example v3.0.0-alpha-3), and how you installed it: the release's folder, or built yourself.
- Any other payloads you load.

## 2. What happened

- What you did, step by step.
- What you expected, and what you saw instead: the exact message, or a photo of the screen.
- Whether it happens every time, and after a full restart too.

## 3. The logs

Easiest, with no PC: turn on Settings, **Logging**, reproduce the problem, then zip the newest folder in
`/data/ps5proton/logs/detailed/` and follow its README.txt. Otherwise:


Copy them **before you open MihawkOS again**: the next launch replaces some of them. Over FTP (usually port 2121):

- the whole `/data/ps5proton/logs/` folder;
- `/data/homebrew/PPSA99170/title-early.log`;
- `/data/ps5proton/setup.txt`, if the guided setup was involved.

For a crash or a hang, a kernel log helps most. With klogsrv loaded, capture it from a PC while you reproduce the
problem: `nc <console-address> 3232 > klog.txt`, then stop it with Ctrl+C.

## 4. Privacy

- Replace your console's address everywhere it appears with `192.168.x.y`.
- Logs can name your programs' folders; remove them if you prefer.
- Never attach game files, saves, keys or firmware.

## If the console hung

Wait at least 3 minutes before cutting power. After restarting, copy the logs before you open MihawkOS.
