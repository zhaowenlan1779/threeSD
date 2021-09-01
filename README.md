threeSD
========

threeSD is a tool to help you import data from a Nintendo 3DS SD Card for [Citra](https://citra-emu.org), or dump CXIs and build CIAs, all directly on your PC!

## Advantages

Compared with the previous method of using [GodMode9](https://github.com/d0k3/GodMode9) to dump games, and [Checkpoint](https://github.com/FlagBrew/Checkpoint) to dump saves, threeSD offers the following advantages:

* Simple to use. You can import everything at once, including applications, updates, DLCs, saves, extra datas as well as necessary system datas. The UI is very simple, but usable and intutive. On your 3DS you will only need to run a GM9 script and everything is ready.
* Fast. A PC's processing power and I/O speeds are obviously much better than a 3DS. In my test, importing all 20+ GiB of content only took about 20 minutes.
* Does not require additional SD card space. `Dumping` a content requires space on your SD card. `Importing` it doesn't.

## Usage Instructions

Please refer to the [wiki](https://github.com/zhaowenlan1779/threeSD/wiki/Quickstart-Guide).

## TODO

* Clean up core/importer.cpp by removing those 00000000...000000s there with FileUtil functions
* UI improvements
    * Better error messages
    * Beautiful icons
* Bug fixes
* Clear all the `TODO`s in the code
* Wireless transfer (probably FTP?)
    * but: slow, complex
