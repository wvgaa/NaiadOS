# Changelog

## 1.0.4 beta

### Added
- Kernel choice in the installer: LTS (default), Stable, or both. With both, Stable boots by default and LTS stays in GRUB as a fallback.
- Bootloader choice in the installer: NaiadOS GRUB (default) or keep the existing bootloader. The screen lists the boot entries already on the machine.
- NaiadOS GRUB detects Windows and other Linux installs with os-prober.
- Manual partitioning with cfdisk as a fourth disk option. Afterwards you assign root, EFI, an optional swap (partition or swapfile) and an optional separate /home (keep or format).
- Windows cleanup: when the install replaces the last Windows partition, the installer offers to remove its boot entries, the EFI/Microsoft folder and the Windows reserved and recovery partitions. Both questions default to no.
- Install log at /var/log/naiadinstall.log on the installed system.
- NaiadOS logo icon at /usr/share/pixmaps/naiados.png, referenced by LOGO=naiados in os-release.
- New fastfetch logo.
- New live ISO welcome message.

### Changed
- The live ISO boots linux-naiados-lts.
- The installer refreshes the mirror list and the pacman keyring after its internet check instead of at boot.
- naiadcheck 1.0-2 includes the SMART improvements from 1.0.3.

### Removed
- BIOS boot. NaiadOS is UEFI only.
- naiadclean and naiadfetch.

### Fixed
- The live ISO waited about 90 seconds at boot on machines without ethernet.
- "Use an existing partition" formatted the selected partition on a single Enter. It now hides the live USB, mounted, EFI and Microsoft reserved partitions, labels the rest, and only formats after typing YES. Erasing a whole disk also needs YES.
- The EFI partition is found by its partition type instead of taking the first FAT partition. With several EFI partitions the installer asks which one to use.
- Mounting could fail right after formatting a former NTFS partition. Old filesystem signatures are wiped before formatting, and every mount names its filesystem type.
- grub-mkconfig failed because the installed /etc/os-release was not quoted.
- Other Linux installs were missing from the boot menu (fuse3 was not installed for os-prober).
- GRUB 2.16 filled the boot menu with an "EFI BootNext" entry for every firmware slot. These entries are disabled.
- fastfetch was not installed although its config was.
- The "chroot into installation" option at the end of the install did not work.

### Kernel
- linux-naiados 7.2.7 and linux-naiados-lts 6.18.53 replace the single 7.0.12 kernel. Both are built from the same debloated config for baseline x86-64.
- Kernel updates now rebuild the initramfs automatically (pacman hook), and removing a kernel removes its initramfs.
- BTF is enabled again, which brings back sched_ext and fixes systemd's bpf-restrict-fs error at boot.
- Modules are zstd compressed. Installed size went from about 495 MB to about 143 MB per kernel.
- Intel Xe graphics driver enabled (Intel Arc, Lunar Lake, Panther Lake).
- SELinux, Smack and TOMOYO removed.

## 1.0.3 beta

### Added
- naiadcheck in the default install.

### Changed
- naiadcheck shows disk name, temperature and power-on hours, and names the exact cause when a disk check fails.

### Fixed
- Password, hostname and username input is sanitized.

## 1.0.2 beta

### Added
- Niri in the window manager list.
- reflector on the live ISO.

### Changed
- GRUB boots without quiet, so boot messages are visible.

### Removed
- GPU driver selection in the installer.

### Fixed
- Installer progress screens stacking on top of each other.

## 1.0.1 beta

- naiadinstall, an ncurses installer with guided disk setup, users, locale, keyboard layout, timezone, network, audio and a list of window managers and desktops.
- linux-naiados 7.0.12, the first debloated NaiadOS kernel.
- archiso based live ISO.
