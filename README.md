# NaiadOS

NaiadOS is an Arch Linux based distribution. It keeps Arch's control and simplicity, ships a debloated kernel and a TUI installer, and leaves the rest to you.

Current version: **1.0.4 beta**. NaiadOS is not stable yet. Try it in a VM or on a spare machine, and back up your data before installing next to another system.

Website: https://naiados.org

## Features

- naiadinstall, an ncurses installer written in C++
  - disk setup: whole disk, free space, an existing partition, or manual partitioning with cfdisk
  - kernel: LTS, Stable, or both
  - bootloader: NaiadOS GRUB with os-prober, or keep the existing one
  - users, locale, keyboard layout, timezone, network, audio and a choice of window managers and desktops
- Two kernels built from a debloated config: linux-naiados (latest stable) and linux-naiados-lts
- naiadcheck, a system health check (updates, disk health, services, orphans)
- UEFI only live ISO

## Repository

| Path | Contents |
|---|---|
| naiadinstall/ | the installer |
| naiadiso/ | archiso profile for the live ISO |
| kernel/ | kernel PKGBUILDs, configs and pacman hooks |
| NaiadTools/ | naiadcheck |
| branding/ | logo |

## Building

Installer:

```
cd naiadinstall
g++ -march=x86-64 main.cpp -o naiadinstall -lncursesw
```

Kernels and naiadcheck are built with makepkg. The ISO expects them in a local package repository (see naiadiso/pacman.conf), then:

```
sudo mkarchiso -v -w /var/tmp/naiadiso-work -o out naiadiso
```

## Roadmap

- 1.0.5: build your own kernel from the installer
- 1.0.6: filesystem choice (ext4, btrfs, XFS, F2FS)
- 1.0 stable: December 2026

## License

GPL-3.0, see LICENSE. The kernel configs in kernel/ are part of the Linux kernel build and are covered by the kernel's GPL-2.0.
