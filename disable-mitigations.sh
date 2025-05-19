#!/bin/bash
set -euo pipefail

# 1. Ensure /host is mounted
if [ ! -f /host/etc/default/grub ]; then
  echo "Error: Host root not mounted at /host. Run with '-v /:/host'." >&2
  exit 1
fi

# 2. Choose your mitigation flags here:
FLAGS="mitigations=off"

# 3. Backup original GRUB file
cp /host/etc/default/grub /host/etc/default/grub.bak

# 4. Append flags if not already present
grep -q "$FLAGS" /host/etc/default/grub || \
  sed -i "s/GRUB_CMDLINE_LINUX_DEFAULT=\"\(.*\)\"/GRUB_CMDLINE_LINUX_DEFAULT=\"\1 $FLAGS\"/" \
      /host/etc/default/grub

# 5. Chroot and update GRUB on the host
mount --bind /dev  /host/dev
mount --bind /proc /host/proc
mount --bind /sys  /host/sys
chroot /host update-grub

# 6. Clean up mounts
umount /host/dev /host/proc /host/sys || true

echo "✅ Host GRUB updated with '$FLAGS'."
echo "→ Reboot the host to apply changes."
