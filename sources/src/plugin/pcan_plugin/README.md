# Dependency install

PCAN drivers (Peak System Interface) are primarily designed for **Windows**.
On Linux, Peak provides support through a combination of **kernel modules**, **libpcan** (userspace library), and **SocketCAN integration**.

---

## 📌 Prerequisites

- A PCAN USB/PCI/PCIe device connected to your system.
- Root privileges (or `sudo`).
- Basic kernel headers installed.

---

## 1. Debian (Ubuntu-based systems)

### Step 1: Install Dependencies

```bash
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r) dkms libpcan
```

> **Note:** `linux-headers-$(uname -r)` ensures you have the correct headers for your running kernel.

### Step 2: Download PCAN Driver

Download the latest PCAN-Linux driver from [Peak System’s official site](https://www.peak-system.com/fileadmin/media/linux/index.htm):

```bash
wget https://www.peak-system.com/fileadmin/media/linux/files/pcan-basic_3.8.0.tar.gz
tar -xzf pcan-basic_3.8.0.tar.gz
cd pcan-basic
```

### Step 3: Compile and Install

```bash
make
sudo make install
```

This installs:
- Kernel module: `pcan.ko`
- Userspace library: `libpcan.so`
- Headers and tools in `/usr/lib/pcan/` and `/usr/include/pcan/`

### Step 4: Load the Kernel Module

```bash
sudo modprobe pcan
```

Check if it’s loaded:

```bash
lsmod | grep pcan
```

### Step 5: Verify Device Detection

```bash
dmesg | grep pcan
```

You should see messages like:
```
pcan: Peak PCAN interface driver loaded
pcan: Found a PCAN-USB device on USB bus ...
```

### Step 6: Configure SocketCAN (Optional but Recommended)

To use PCAN with `can-utils` or applications via SocketCAN:

```bash
sudo ip link set can0 type can bitrate 500000
sudo ip link set up can0
```

Test with `candump` or `cansend` from `can-utils`:

```bash
sudo apt install can-utils
candump can0
```

---

## 2. Arch Linux

Arch Linux uses `pacman` and the `AUR` (Arch User Repository).

### Option A: Install via AUR (Recommended)

Use an AUR helper like `yay` or `paru`:

```bash
yay -S pcan-basic
```

This installs:
- Kernel module (`pcan`)
- `libpcan`
- Header files

After installation, reload the module:

```bash
sudo modprobe pcan
```

### Option B: Manual Installation from Source

If you prefer manual control:

```bash
# Install dependencies
sudo pacman -S base-devel linux-headers dkms libpcan

# Download source
wget https://www.peak-system.com/fileadmin/media/linux/files/pcan-basic_3.8.0.tar.gz
tar -xzf pcan-basic_3.8.0.tar.gz
cd pcan-basic

# Compile and install
make
sudo make install
sudo modprobe pcan
```

### Verify Installation

```bash
lsmod | grep pcan
dmesg | grep pcan
```

---

## 🔧 Troubleshooting

### Module Not Loading

```bash
sudo depmod -a
sudo modprobe pcan
```

Check for errors:

```bash
dmesg | tail
```

Common issues:
- Wrong kernel headers.
- USB device not detected (`lsusb` should show Peak device).

### libpcan Not Found

Ensure `/usr/lib/pcan` is in your library path:

```bash
echo "/usr/lib/pcan" | sudo tee /etc/ld.so.conf.d/pcan.conf
sudo ldconfig
```

### SocketCAN Interface Missing

After loading `pcan`, the interface may not appear as `can0`. You can create it manually:

```bash
sudo ip link add can0 type pcan
sudo ip link set can0 up
```

Or use `pcanview` (GUI tool) to detect devices.

---

## 🧪 Testing with pcantool

Peak provides a command-line tool for testing:

```bash
sudo /usr/lib/pcan/pcantool
```

This will detect PCAN devices and allow basic CAN communication tests.

---

## 📚 Additional Resources

- [Peak Linux Driver Documentation](https://www.peak-system.com/fileadmin/media/linux/index.htm)
- [SocketCAN Wiki](https://www.kernel.org/doc/Documentation/networking/can.txt)
- [can-utils Documentation](https://github.com/linux-can/can-utils)

---

## ✅ Summary

| Step | Debian | Arch Linux |
|------|--------|------------|
| Install deps | `apt install build-essential linux-headers-$(uname -r) dkms libpcan` | `pacman -S base-devel linux-headers dkms libpcan` |
| Install driver | Download & compile from source | `yay -S pcan-basic` (AUR) |
| Load module | `sudo modprobe pcan` | `sudo modprobe pcan` |
| Verify | `lsmod \| grep pcan`, `dmesg \| grep pcan` | Same |
| SocketCAN setup | `ip link set can0 type can ...` | Same |
