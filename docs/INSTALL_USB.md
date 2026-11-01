# Install MihawkOS's Windows runtime from a USB drive

Up to Alpha 6, the QR code in MihawkOS's setup opened this page, which showed how to prepare a USB drive.

Since Alpha 7 there is no USB drive and no FTP step. The Windows runtime ships inside the `PPSA99170` folder, and the
setup installs it from there by itself:

1. Download the newest release and unzip it.
2. Copy its `PPSA99170` folder to `/data/homebrew/PPSA99170/`, in place of an older one.
3. Open MihawkOS. On a first install, the setup walks you through the rest; on an update, MihawkOS updates its runtime
   by itself.

See the README's [Get started](../README.md#get-started). For Alpha 6 or older, this page's USB steps are in that
release's source, at its tag.
