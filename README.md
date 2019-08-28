threeSD
========

threeSD is a tool to help prepare your system for the Nintendo 3DS emulator [Citra](https://citra-emu.org).

## Instructions

First of all, of course, you should download a [release](https://github.com/zhaowenlan1779/threeSD/releases) of threeSD and extract the archive somewhere.

### What you'll need

* Nintendo 3DS with access to CFW and [GodMode9](https://github.com/d0k3/GodMode9)
  * If your 3DS is not yet hacked, you can do so by following the instructions [here](https://3ds.hacks.guide).
  * You can install GodMode9 by downloading it and copying the `firm` file to `luma/payloads` on your 3DS. You can rename it to begin with `[BUTTON]_` (e.g. `X_GodMode9.firm`) to set a convenicence button to hold during boot to enter GodMode9.
* PC compatible with Citra
  * You will need a graphics card compatible with OpenGL 3.3 and install the latest graphics drivers from your vendor's website.
  * Operating system requirements: **64-bit** Windows (7+), Linux (flatpak) or macOS (10.13+). Note that Citra on macOS 10.13 is currently broken. It is recommended to update to 10.14.
* SD / microSD card reader

### On Your 3DS

You will need to run a GodMode9 script. If you are unsure about the script's safety (which is good!), check the source code yourself [here](https://github.com/zhaowenlan1779/threeSD/blob/master/dist/threeSDumper.gm9).

1. Copy the gm9 script (`threeSDumper.gm9`) in `dist` to the `gm9/scripts` folder on your SD card.
1. Power up your 3DS to launch GodMode9 (you will need to hold a button corresponding to your `firm` file's name, or hold `START` to enter the chainloader). Press the `Home` button to bring up GodMode9's `HOME Menu`. Use the d-pad and the `A` button to select `Scripts...`.
1. Use the d-pad and the `A` button to select `threeSDumper`. You will be prompted with a question "Execute threeSD Dumper?". Press `A` to confirm.
1. After a moment or two you will see the message "Successfully dumped necessary files for threeSD." Your 3DS SD card is now prepared for use with threeSD and Citra. Press `A` to exit the script.
1. Power off your 3DS with `R+START`. Remove the SD card from your 3DS and insert it into your PC (with a card reader).

### On your PC

Make sure the SD card is properly recognized and shows up as a disk.

1. 
