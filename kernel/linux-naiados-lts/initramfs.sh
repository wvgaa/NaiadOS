#!/bin/sh
# Rebuilds (or on removal deletes) the linux-naiados-lts initramfs.
# Skipped when the preset is missing: during pacstrap and mkarchiso the
# preset is written separately and mkinitcpio runs afterwards anyway.
preset=/etc/mkinitcpio.d/linux-naiados-lts.preset

case "$1" in
  remove)
    rm -f /boot/initramfs-naiados-lts.img /boot/initramfs-naiados-lts-fallback.img
    ;;
  *)
    [ -f "$preset" ] || exit 0
    [ -f /boot/vmlinuz-naiados-lts ] || exit 0
    exec mkinitcpio -p linux-naiados-lts
    ;;
esac
