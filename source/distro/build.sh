#!/bin/bash

if [ -n "$SUDO_USER" ]; then
    REAL_HOME=$(getent passwd "$SUDO_USER" | cut -d: -f6)
else
    REAL_HOME="$HOME"
fi

PROJECT_DIR="$REAL_HOME/Flosh/distro"

if [ "$EUID" -ne 0 ]; then
    echo "Please, run with sudo: sudo ./build.sh"
    exit 1
fi

echo
echo " -= Flosh Distro Build Script 1.0 =-"
echo

cd "$PROJECT_DIR" || { echo "Couldn\'t go to $PROJECT_DIR"; exit 1; }

echo "Building binaries..."
gcc -static -o init binaries/init.c
gcc -static -o flosh binaries/flosh.c
mv init rootfs/
mv flosh rootfs/bin/

echo "Creating initramfs..."
cd rootfs
find . | cpio -o -H newc | gzip > "$PROJECT_DIR/initramfs.cpio.gz"
cd ..

echo "Preparing IMG with dynamic size..."
DISK_IMAGE="$PROJECT_DIR/flosh.img"
MOUNT_DIR="/mnt/flosh_build"

# ---- Автоматический расчёт размера ----
ESP_MB=64

# Создаём временную папку для подсчёта содержимого корневого раздела
TEMP_ROOT=$(mktemp -d)
cp "$PROJECT_DIR/../kernel/arch/x86/boot/bzImage" "$TEMP_ROOT/vmlinuz"
cp "$PROJECT_DIR/initramfs.cpio.gz" "$TEMP_ROOT/initrd.gz"
mkdir -p "$TEMP_ROOT/drive"
cp -r "$PROJECT_DIR/drive/." "$TEMP_ROOT/drive" 2>/dev/null

ROOT_BYTES=$(du -sb "$TEMP_ROOT" | cut -f1)
ROOT_MB=$(( (ROOT_BYTES + 1024*1024 - 1) / (1024*1024) ))  # округление вверх
ROOT_MB=$(( ROOT_MB * 120 / 100 + 10 ))   # запас 20% + 10 МБ
[ $ROOT_MB -lt 64 ] && ROOT_MB=64         # минимум 64 МБ

TOTAL_MB=$((ESP_MB + ROOT_MB + 1))
echo "Root content: $((ROOT_BYTES / 1024 / 1024)) MB, with overhead: $ROOT_MB MB"
echo "Total image size: $TOTAL_MB MB"
rm -rf "$TEMP_ROOT"

# Создаём образ нужного размера
dd if=/dev/zero of="$DISK_IMAGE" bs=1M count=$TOTAL_MB status=progress

# Разметка GPT
parted -s "$DISK_IMAGE" mklabel gpt
parted -s "$DISK_IMAGE" mkpart primary fat32 1MiB ${ESP_MB}MiB
parted -s "$DISK_IMAGE" set 1 esp on
parted -s "$DISK_IMAGE" mkpart primary ext4 ${ESP_MB}MiB 100%

LOOP=$(losetup -f --show -P "$DISK_IMAGE")
mkfs.vfat -F 32 -n FLOSH_ESP "${LOOP}p1"
mkfs.ext4 -F -L FLOSH_ROOT "${LOOP}p2"

mkdir -p "$MOUNT_DIR/esp" "$MOUNT_DIR/root"
mount "${LOOP}p1" "$MOUNT_DIR/esp"
mount "${LOOP}p2" "$MOUNT_DIR/root"

# Копирование файлов в корневой раздел
#cp "$PROJECT_DIR/../kernel/arch/x86/boot/bzImage" "$MOUNT_DIR/root/vmlinuz"
cp "$PROJECT_DIR/initramfs.cpio.gz" "$MOUNT_DIR/root/initrd.gz"
mkdir -p "$MOUNT_DIR/root/drive"
cp -r "$PROJECT_DIR/drive/." "$MOUNT_DIR/root/drive" 2>/dev/null

# Копирование ядра и initrd на ESP (для загрузчика)
#cp "$MOUNT_DIR/root/vmlinuz" "$MOUNT_DIR/esp/vmlinuz"
cp "$PROJECT_DIR/../kernel/arch/x86/boot/bzImage" "$MOUNT_DIR/esp/vmlinuz"
cp "$MOUNT_DIR/root/initrd.gz" "$MOUNT_DIR/esp/initrd.gz"

# Установка Syslinux
mkdir -p "$MOUNT_DIR/esp/EFI/BOOT"
mkdir -p "$MOUNT_DIR/esp/boot/syslinux"
cp /usr/lib/syslinux/efi64/syslinux.efi "$MOUNT_DIR/esp/EFI/BOOT/BOOTX64.EFI"
cp /usr/lib/syslinux/efi64/*.c32 "$MOUNT_DIR/esp/boot/syslinux/"
cp /usr/lib/syslinux/efi64/ldlinux.e64 "$MOUNT_DIR/esp/boot/syslinux/"

# Конфиг Syslinux
cat > "$MOUNT_DIR/esp/boot/syslinux/syslinux.cfg" <<EOF
UI menu.c32
MENU TITLE Flosh Boot Menu
TIMEOUT 50
DEFAULT flosh

LABEL flosh
    MENU LABEL Flosh Linux 1.0
    LINUX /vmlinuz
    INITRD /initrd.gz
    APPEND root=/dev/sda2 rw console=tty0 init=/init quiet loglevel=0 video=1280x1080@60

LABEL flosh-debug
    MENU LABEL Flosh Linux 1.0 (debug)
    LINUX /vmlinuz
    INITRD /initrd.gz
    APPEND root=/dev/sda2 rw console=tty0 init=/init quiet video=1280x1080@60
EOF

# Размонтируем и освобождаем loop
sync
umount "$MOUNT_DIR/esp" "$MOUNT_DIR/root"
losetup -d "$LOOP"
rmdir "$MOUNT_DIR/esp" "$MOUNT_DIR/root" "$MOUNT_DIR"

echo "Done. Image created: $DISK_IMAGE"

# Запуск QEMU для теста (если нужен)
if [ -f /usr/share/edk2/x64/OVMF.4m.fd ]; then
    qemu-system-x86_64 -bios /usr/share/edk2/x64/OVMF.4m.fd -hda "$DISK_IMAGE" -m 512M
else
    echo "OVMF not found, skipping QEMU."
fi
