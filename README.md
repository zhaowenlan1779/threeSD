threeSD
========

threeSD is a tool to help prepare your system for the Nintendo 3DS emulator [Citra](https://citra-emu.org).

## Advantages

Compared with the previous method of using [GodMode9](https://github.com/d0k3/GodMode9) to dump games, and [Checkpoint](https://github.com/FlagBrew/Checkpoint) to dump saves, threeSD offers the following advantages:

* Simple to use. You can import everything at once, including applications, updates, DLCs, saves, extra datas as well as necessary system datas. The UI is very simple, but usable and intutive. On your 3DS you will only need to run a GM9 script and everything is ready.
* Fast. A PC's processing power and I/O speeds are obviously much better than a 3DS. In my test, importing all 20+ GiB of content only took about 20 minutes.
* Does not require additional SD card space. `Dumping` a content requires space on your SD card. `Importing` it doesn't.

## Usage Instructions

First of all, of course, you should download a [release](https://github.com/zhaowenlan1779/threeSD/releases) of threeSD and extract it somewhere.

If you are wishing to use threeSD with a portable install of Citra (i.e. that has a `user` folder), click `Customize...` in the main dialog and change the `Citra User Path` field.

### What you'll need

* Nintendo 3DS with access to CFW and GodMode9
    * Both New/Old are okay, but Citra only emulates the Old 3DS currently and New 3DS exclusive games won't work.
    * If your 3DS is not yet hacked, you can hack it by following the instructions [here](https://3ds.hacks.guide).
    * You can install GodMode9 by downloading it and copying the `firm` file to `luma/payloads` on your SD card. You can rename it to begin with `[BUTTON]_` (e.g. `X_GodMode9.firm`) to set a convenicence button to hold during boot to enter GodMode9.
* PC compatible with Citra
    * You will need a graphics card compatible with OpenGL 3.3 and install the latest graphics drivers from your vendor's website.
    * Operating system requirements: **64-bit** Windows (7+), Linux (flatpak compatible) or macOS (10.13+).
* SD / microSD card reader
    * Make sure it can be well connected to your PC (i.e. do not use a 10-year-old dusty one)

### On Your 3DS

You will need to run a GodMode9 script. If you are unsure about the script's safety (which is good!), check the source code yourself [here](https://github.com/zhaowenlan1779/threeSD/blob/master/dist/threeSDumper.gm9).

1. Copy the gm9 script (`threeSDumper.gm9`) in `dist` to the `gm9/scripts` folder on your SD card.
1. Launch GodMode9 on your 3DS (you will need to hold a button corresponding to your `firm` file's name, or hold `START` to enter the chainloader menu). Press the `Home` button to bring up GodMode9's `HOME Menu`. Use the d-pad and the `A` button to select `Scripts...`.
1. Use the d-pad and the `A` button to select `threeSDumper`. You will be prompted with a question "Execute threeSD Dumper?". Press `A` to confirm.
1. After a few seconds, you will see the message "Successfully dumped necessary files for threeSD." Your 3DS SD card is now prepared for use with threeSD and Citra. Press `A` to exit the script.
1. Power off your 3DS with `R+START`. Remove the SD card from your 3DS and insert it into your PC (with a card reader).

### On your PC

Make sure the SD card is properly recognized and shows up as a disk.

1. Launch threeSD. You should see a small dialog, which has your SD card as an auto-detected configuration. 
    * If it does not show up and the combo box says `None`, you should check if you can really find your SD card in the explorer (aka. `My Computer`), whether the drive for your SD card is accessible, and whether it contains the `Nintendo 3DS` and `threeSD` folders.
1. Click `OK`. After a few seconds of loading, you should see the `Select Contents` dialog. Select the contents you would like to import. By default, contents that do not currently exist is selected. Make sure the total size of your selected contents do not exceed the available space on your disk.
    * You can select between `Title View` which organizes contents by title, and `Group View` which organizes contents by type (application, save data, etc).
    * The `System Archive` and `System Data` groups contains important data that is necessary for your imported games to run. You should definitely import the contents there, if they do not exist yet.
1. After you've finished your selection, click `OK`. You should now see a progress dialog; wait a while until your contents are imported.
    * The time will depend on how big your contents are, as well as your CPU processing power and (mainly) disk I/O speeds.

### What to do next

You can now enjoy your games with Citra, at high resolutions, with custom controllers, and the (now in Canary) Custom Textures feature!

It is recommended that you also optionally [dump your config savegame](https://citra-emu.org/wiki/dumping-config-savegame-from-a-3ds-console) if you come across problems, for the best experience while enjoying Citra.

If you have any game cartidges, and would like to dump them as well, visit [this tutorial](https://citra-emu.org/wiki/dumping-game-cartridges).

## TODO

* Config savegame
* UI improvements
    * Better error messages
    * Beautiful icons
* Bug fixes
* Clear all the `TODO`s in the code
* Wireless transfer (probably FTP?)
    * but: slow, complex
