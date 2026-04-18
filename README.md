# MyDistro — Project Description

## What Is MyDistro?

MyDistro is a custom Linux distribution created by modifying Kali Linux inside
VirtualBox. It is built using Debian's official `live-build` system, which
packages a full operating system — software, settings, and branding — into a
single bootable `.iso` file that can be run live from USB or installed on any
machine.

---

## Why Build a Custom Distro?

Kali Linux is a powerful platform, but it ships with hundreds of tools most
users never need. MyDistro solves this by starting from Kali's package
repositories and building a leaner, personalized system that includes only
what you actually use — while keeping the full power of Kali's toolset available
on demand via `apt`.

---

## What Makes MyDistro Different?

| Aspect           | Kali Linux (Stock)         | MyDistro                        |
|------------------|----------------------------|---------------------------------|
| Branding         | Kali Linux / OffSec        | Your name, logo, and identity   |
| Tools included   | 600+ security tools        | Only the tools you choose       |
| Default desktop  | XFCE (Kali themed)         | Customized look and feel        |
| GRUB label       | "Kali GNU/Linux"           | "MyDistro"                      |
| OS release info  | Kali Linux                 | Your distro name and version    |
| ISO size         | ~4 GB                      | Smaller — only your packages    |

---

## How It Works

The build process runs inside a Kali Linux virtual machine in VirtualBox.
The `live-build` tool reads your configuration — package lists, custom files,
and branding — and automatically downloads, assembles, and compresses
everything into a bootable ISO.

```
Your Config Files
      │
      ▼
  live-build
      │
      ├── Downloads packages from kali-rolling repo
      ├── Applies your custom /etc/os-release branding
      ├── Copies your wallpapers and settings
      ├── Compresses the root filesystem (squashfs)
      └── Wraps everything into a bootable .iso
            │
            ▼
   live-image-amd64.hybrid.iso  ← Your custom distro
```

---

## Use Cases

- **Personal workstation OS** — a trimmed-down Kali with your daily tools
- **Learning project** — understand how Linux distributions are built
- **Portable environment** — boot from USB on any computer
- **Team distribution** — share a pre-configured environment with your team
- **CTF / lab environment** — customized toolset for competitions or labs

---

## Technical Stack

| Component       | Technology                            |
|-----------------|---------------------------------------|
| Base distro     | Kali Linux (kali-rolling branch)      |
| Package manager | APT (Debian-compatible)               |
| Build tool      | `live-build` (Debian Live project)    |
| Filesystem      | SquashFS (compressed read-only layer) |
| Boot format     | ISOLINUX + GRUB2 hybrid ISO           |
| Desktop         | XFCE (customizable)                   |
| Architecture    | x86_64 (amd64)                        |
| Virtualization  | Oracle VirtualBox                     |

---

## Project Scope

This project covers:

1. Setting up Kali Linux in VirtualBox as a build environment
2. Configuring `live-build` with custom package lists and branding files
3. Building a bootable ISO image from scratch
4. Testing the ISO in a second VirtualBox VM
5. Optionally writing the ISO to a USB drive for physical use

It does NOT cover:
- Setting up an online repository or update server
- Signed packages or custom package signing
- Creating an installer (only a live system is produced)

---

## Version History

| Version | Date       | Notes                        |
|---------|------------|------------------------------|
| 1.0     | 2026-04-18 | Initial release, XFCE desktop |

---

## Author

Created by: **Your Name**
Contact: your@email.com
Repository: https://github.com/yourname/mydistro
