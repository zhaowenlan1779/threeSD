threeSD
========

threeSD is a tool to help prepare your system for the Nintendo 3DS emulator [Citra](https://citra-emu.org).

## Advantages

Compared with the previous method of using [GodMode9](https://github.com/d0k3/GodMode9) to dump games, and [Checkpoint](https://github.com/FlagBrew/Checkpoint) to dump saves, threeSD offers the following advantages:

* Simple to use. You can import everything at once, including applications, updates, DLCs, saves, extra datas as well as necessary system datas. The UI is very simple, but usable and intutive. On your 3DS you will only need to run a GM9 script and everything is ready.
* Fast. A PC's processing power and I/O speeds are obviously much better than a 3DS. In my test, importing all 20+ GiB of content only took about 20 minutes.
* Does not require additional SD card space. `Dumping` a content requires space on your SD card. `Importing` it doesn't.

## Usage Instructions

Please refer to the [wiki](https://github.com/zhaowenlan1779/threeSD/wiki/Quickstart-Guide).

## TODO

* UI improvements
    * Better error messages
    * Beautiful icons
* Bug fixes
* Clear all the `TODO`s in the code
* Wireless transfer (probably FTP?)
    * but: slow, complex
* MSVC build doesn't work for its crappy preprocessor, but this shouldn't really matter
