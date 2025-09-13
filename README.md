# PrimU2

**PrimU2** is a prototype High-Level Emulator (HLE) for the **HP Prime** calculator (V1 / V2 / G1) built on top of the [Unicorn Engine](https://github.com/unicorn-engine/unicorn).

PrimU2 currently targets the HP Prime firmware **20130808**, because that version does not require `armfir.dat` to be loaded into RAM.

---

## What works

* Filesystem (virtualized, rooted at `.\prime_data`)
* LCD
* Threading
* PE Loader
* ELF Loader
* Power Off
* UI Events (buggy touch support, full keypad support)
* And many other things...

## What doesn't work

* GDI-style drawing API
* Hardware interrupts
* `_OpenFile` and other loader API for reading `armfir.dat`
* Firmware randomly crashes occasionally
* Cross-platform support

---

## Compiling

Visual Studio 2022 or later is required.

1. Open `PrimU.sln` in Visual Studio.
2. Build the solution.

---

## Running

You must first extract `armfir.elf` from the **201308080** firmware update for the calculator.

> The `armfir.elf` file is contained inside the firmware update: the APPDISK.DAT file contains a FAT-16 filesystem starting at an 8 KB offset. Mount that filesystem (or extract it with a suitable tool) to obtain `armfir.elf`. See the HP Prime firmware wiki for more details:
> [https://tiplanet.org/hpwiki/index.php?title=HP\_Prime/Firmware\_files](https://tiplanet.org/hpwiki/index.php?title=HP_Prime/Firmware_files)

Once you have `armfir.elf`, run PrimU with:

```bash
PrimU.exe [path/to/armfir.elf]
```

---

## License

This project is released under the **GPL v2**.

---

## See also

* [PrimeU](https://github.com/opcod3/PrimeU)
* [qemuPrime](https://github.com/Gigi1237/qemuPrime)
* [ripem](https://github.com/boricj/ripem)
* [Linux-For-HPPrime-V2](https://github.com/Repeerc/Linux-For-HPPrime-V2)
* [prinux (G2)](https://github.com/zephray/prinux)
* [Project-Muteki](https://github.com/Project-Muteki)
