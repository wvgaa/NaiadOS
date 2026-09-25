#!/usr/bin/env bash
# shellcheck disable=SC2034

iso_name="naiados"
iso_label="NAIADOS_$(date --date="@${SOURCE_DATE_EPOCH:-$(date +%s)}" +%Y%m)"
iso_publisher="NaiadOS"
iso_application="NaiadOS Live"
iso_version="$(date --date="@${SOURCE_DATE_EPOCH:-$(date +%s)}" +%Y.%m.%d)"
install_dir="naiados"
buildmodes=('iso')
bootmodes=('uefi.grub')
pacman_conf="pacman.conf"
airootfs_image_type="erofs"
airootfs_image_tool_options=('-zlzma,109' -E 'ztailpacking')
bootstrap_tarball_compression=(xz -9e)
file_permissions=(
  ["/etc/shadow"]="0:0:400"
  ["/usr/bin/naiadinstall"]="0:0:755"
)
