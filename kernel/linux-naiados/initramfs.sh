#!/bin/sh
# Rebuilds (or on removal deletes) the linux-naiados initramfs.
# Skipped when the preset is missing: during pacstrap and mkarchiso the
# preset is written separately and mkinitcpio runs afterwards anyway.
preset=/etc/mkinitcpio.d/linux-naiados.preset

case "$1" in
  remove)
    rm -f /boot/initramfs-naiados.img /boot/initramfs-naiados-fallback.img
    ;;
  *)
    [ -f "$preset" ] || exit 0
    [ -f /boot/vmlinuz-naiados ] || exit 0
    exec mkinitcpio -p linux-naiados
    ;;
esac
