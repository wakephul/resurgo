<p align="center"><img src="brand/logo/resurgo-readme.svg" width="96" alt="Resurgo logo: a red lowercase r whose arm rises"></p>

# Resurgo

Resurgo is a free, lightweight Linux distribution that makes old computers usable again, even 15 or 20 year old ones. It is based on Alpine Linux, runs on 32-bit and 64-bit PCs and on ARM boards, and it is meant to be secure and private right after install, with nothing to configure.

In italiano: [README.it.md](README.it.md)

The project has just started and there is nothing to download yet. What follows is the plan.

## The name

Resurgo is Latin for "I rise again". It comes from the phoenix motto *Post fata resurgo*, after death I rise again. The phoenix is reborn from its ashes, and I want to do the same with old computers that everyone has given up on, even though they still work. The logo is an r with its arm raised.

## How I want it to work

It adapts to the machine you put it on. At boot it checks how much RAM there is, which CPU, which disk and whether the fans can be controlled, then tunes itself: with little memory it compresses RAM and keeps fewer browser tabs alive, if the CPU runs hot it lowers the clock, on a spinning disk it stops indexing. The program doing this is called Mantice. It shows you what it found and what it changed, and you can undo each change.

It has its own browser, Favilla, which also runs on 32-bit CPUs and blocks ads and trackers before they load. Ads are also blocked for the whole system at the DNS level.

The disk is encrypted, the system is read-only and updates arrive as a whole image: if one goes wrong, the next boot falls back to the previous version. There is no telemetry and you don't need an account.

The desktop looks modern but has no animations and no transparency, so it stays fast on graphics cards from that era.

## Hardware

| CPU | Examples |
| --- | --- |
| 32-bit x86 (i686) | Pentium 4, Pentium M, Core Duo, netbook Atoms |
| 64-bit x86 | Core 2 Duo, Athlon 64 and everything after |
| 32 and 64-bit ARM | Raspberry Pi 2 and later |
| RISC-V | VisionFive and Milk-V boards, experimental |

You need at least 512 MB of RAM (1 GB is comfortable) and 4 GB of disk. CPUs without SSE2 (Pentium III, Athlon XP) and PowerPC Macs are not supported, because a modern browser can't be built for them.

## How to install

I want anyone to manage it, including people who have never installed an operating system.

When it is ready, installing will work like this.

1. Download one file, `resurgo-pc.iso`. It works on every PC, 32 or 64-bit: Resurgo picks the right version at boot.
2. Write it to any USB stick of 1 GB or more with [Raspberry Pi Imager](https://www.raspberrypi.com/software/), which is free and runs on Windows, macOS and Linux. Skip the device filter, pick "Use custom" at the bottom of the operating system list, choose the file, then the stick. The file stays under 700 MB, so if the computer is too old to boot from USB you can burn the same file to a CD.
3. Plug the stick into the old computer, turn it on and press the boot menu key (see the table below).
4. Resurgo starts from the stick and you can try it without touching what is on the disk.
5. If you like it, click "Install". It asks three things: language, disk, and a name with a password. The same password unlocks the encrypted disk and logs you in, so there is only one.

If the disk has Windows on it, Resurgo offers to copy your documents, photos and desktop to another USB stick or an external drive before it erases anything.

| Brand | Boot menu key |
| --- | --- |
| Dell | F12 |
| HP | Esc, then F9 |
| Lenovo | F12 (the small Novo button on IdeaPads) |
| Acer | F12, sometimes you have to enable it in the BIOS first |
| Asus | Esc or F8 |
| Toshiba | F12 |
| Fujitsu | F12 |
| MSI | F11 |
| Sony Vaio | F11 or the ASSIST button |
| Intel Mac | hold Alt (Option); on 2018 and later Macs with the T2 chip, allow USB boot first in Startup Security Utility |

If your brand isn't listed, look at the very first screen when the computer starts: the key is usually written at the bottom.

Later there will also be a Mac app that runs Resurgo in a virtual machine, no USB stick needed.

## Where we are

Done, in virtual machines (numbers are in `docs/`, in Italian):

1. Minimal image booting on x86, x86_64 and aarch64, under 300 MB without the browser
2. Favilla: on 32-bit JavaScript has no JIT, a tab costs 40 to 140 MB
3. Mantice: with six sites at 512 MB everything loads, without it the system stalls for minutes
4. Signed A/B updates: pulling the power mid-update boots the previous version
5. Panel, menu, settings, welcome screen and theme, tested at 1024×600
6. Fans, temperature and battery in Mantice; armv7 boots to the desktop

Still missing:

- tests on real computers, which matter most for fans and temperature
- RISC-V actually booting (the image builds, the firmware stops in QEMU)
- version 0.1 to download

## Helping out

The most useful thing right now is telling me which computer you'd like to run it on. Open an issue with brand, model, RAM and year if you know it. It helps me choose the test machines and the fans to support first.

## License

Code is GPL-3.0-or-later. Logo, images and documentation are CC BY-SA 4.0.
