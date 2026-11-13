# Optional KVMFR transport

The default transport needs no KVMFR module. Use this only if you deliberately
prefer KVMFR or your guest kernel policy rejects raw PCI sysfs mapping.

Inside the Linux guest, install DKMS and headers matching its running kernel
(for the standard CachyOS kernel, usually `linux-cachyos-headers`). After
`bash build.sh` has fetched the pinned sources:

```bash
(
  cd _upstream/LookingGlass/module
  sudo dkms install .
)
sudo install -Dm644 config/70-kvmfr.rules /etc/udev/rules.d/70-kvmfr.rules
sudo install -Dm644 config/kvmfr-guest.conf /etc/modules-load.d/kvmfr.conf
sudo udevadm control --reload-rules
sudo modprobe kvmfr
sudo udevadm trigger --subsystem-match=kvmfr
LG_SHM=/dev/kvmfr0 bash run-guest.sh
```

The guest driver must bind to the existing IVSHMEM PCI device. Do not set
`static_size_mb` inside the guest: that would allocate unrelated guest memory.
Choose the correct `/dev/kvmfrN` if more than one device exists. Do not replace
an existing DKMS installation without checking its version/source first.

The physical host can still use the ordinary `/dev/shm/looking-glass` backing
file and unchanged B7 client. Using a host-side KVMFR allocation for GPU imports
is a separate option and requires changing the physical host/QEMU configuration;
it is not required here. Guest/physical-host `/dev/kvmfrN` names are independent.

To return to raw mode, remove the guest KVMFR autoload configuration, stop
capture, and either reboot without the module or unbind only the selected
IVSHMEM device from its driver. Then run `setup-ivshmem.sh` and start capture
without `LG_SHM=/dev/kvmfrN`. Never unbind the NVIDIA GPU to change this transport.
