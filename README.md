## Fork of Zenith432's GenericUSBXHCI USB 3.0 Driver by RehabMan


### How to Install:

Install GenericUSBXHCI.kext using Kext Wizard or your favorite kext installer.


### Downloads:

Downloads are available on Google Code:

https://code.google.com/p/os-x-generic-usb3/downloads/list


### Build Environment

My build environment is currently Xcode 4.61, using SDK 10.8, targeting OS X 10.7.

No other build environment is supported.


### 32-bit Builds

This project does not support 32-bit builds.  It is coded for 64-bit only.


### Source Code:

The source code is maintained at the following sites:

https://code.google.com/p/os-x-generic-usb3/

https://github.com/RehabMan/OS-X-Generic-USB3


### Feedback:

Please use the following thread on tonymacx86.com for feedback, questions, and help:

TODO: provide link


### Known issues:


### Change Log:

2013-03-23 (RehabMan)

- Modified for single binary to work on ML, Lion (10.7.5 only)

- Optimize build to reduce code size and exported symbols.


2013-03-06 (Zenith432)

- Initial build provided by Zenith432 on insanelymac.com


### History

This repository contains a modified version of Zenith432's GenericUSBXHCI USB 3.0 driver.  All credits to Zenith432 for the original code and probably further enhancements/bug fixes.

Original sources came from this post on Insanely Mac:

http://www.insanelymac.com/forum/topic/286860-genericusbxhci-usb-30-driver-for-os-x-with-source/

Original repo:

http://sourceforge.net/p/genericusbxhci/code

My goal in creating this repository was just to create a single binary that could be used on 10.8.x, 10.7.5.  I simply optimized the build settings for a smaller binary, removed some of the #if conditionals and added runtime checks as appropriate for differences between versions.  Having a single optimized build for the Probook Installer makes the package smaller and easier to manage.

If you install my version on a 10.7.4 or prior, the driver will gracefully exit.
