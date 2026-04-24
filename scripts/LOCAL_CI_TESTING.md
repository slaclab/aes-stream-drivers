# Local CI Testing

Run the full CI pipeline locally before pushing — catch failures in seconds
instead of waiting for 10-minute GitHub Actions cycles.

## Quick Start

```bash
# First run: provisions the parity VM (needs KVM + ~10 min first time)
./scripts/run_ci_parity.sh --no-hang-repro

# Run the full CPU + GPU matrix (mirrors ci_pipeline.yml)
./scripts/run_ci_parity.sh --matrix
```

## Prerequisites

- **Linux host** with KVM (RHEL 9, Ubuntu 22.04+, Debian 12+)
- `/dev/kvm` accessible (your user in the `kvm` group)
- ~8 GB free RAM, ~20 GB free disk for the VM image
- See `scripts/ci-local/README.md` for detailed prerequisites

## What It Does

The parity harness provisions a KVM guest with the same Azure-flavor kernel
that GitHub Actions runners use, then executes the CI matrix inside Docker
containers on that guest. The `scripts/ci/*.sh` modules run verbatim — no
local shims or forks.

### Stages

| Stage | Script | What it does |
|-------|--------|--------------|
| 1 | preflight_kvm.sh | Verifies KVM is usable on this host |
| 2 | install-host-deps.sh | Installs qemu-kvm, libvirt, virtiofsd |
| 3 | provision_vm.sh | Downloads Ubuntu 24.04 cloud image, installs Azure kernel + Docker 28.x |
| 4 | hang_repro.sh | Reproduces ubuntu:24.04 insmod hang for diagnosis |
| 5 | run_matrix.sh --phase cpu | Runs all CPU distro cells |
| 6 | run_matrix.sh --phase gpu | Runs all GPU distro cells |

### Matrix Cells

Mirrors `.github/workflows/ci_pipeline.yml` exactly. All cells declare
`load_test: true`; the actual load/test/unload steps only fire on the
cell whose kernel matches the parity VM host (driven by
`CI_HOST_MATCH=1`). Other cells stop at build + DKMS tarball smoke.

| Container | CPU | GPU | Load/test gating |
|-----------|-----|-----|------------------|
| ubuntu:24.04 | yes | yes | Runs load+test when `CI_HOST_MATCH=1` (normally the matching cell) |
| ubuntu:22.04 | yes | yes | Build + DKMS smoke unless `CI_HOST_MATCH=1` |
| rockylinux:9 | yes | yes | Build + DKMS smoke unless `CI_HOST_MATCH=1` |
| debian:experimental | yes | yes | Build + DKMS smoke unless `CI_HOST_MATCH=1` |
| fedora:rawhide | yes | yes | Build + DKMS smoke unless `CI_HOST_MATCH=1` |

## Common Flags

```bash
./scripts/run_ci_parity.sh --matrix          # Full CPU + GPU matrix
./scripts/run_ci_parity.sh --no-hang-repro   # Skip hang reproduction stage
./scripts/run_ci_parity.sh --reset           # Tear down + re-provision from scratch
./scripts/run_ci_parity.sh --clean           # Tear down VM and exit
```

## Environment Variables

| Variable | Default | Purpose |
|----------|---------|---------|
| AES_CI_CACHE_DIR | ~/.cache/aes-ci-parity | VM image and SSH key cache |
| AES_CI_VM_MEMORY | 4096 | Guest RAM (MB) |
| AES_CI_VM_VCPUS | 4 | Guest vCPU count |
| AES_CI_DOMAIN | aes-ci | libvirt domain name |
| AES_CI_CELL_TIMEOUT_SEC | 900 | Per-cell wall-clock limit (seconds) |

## Logs

Per-cell logs land in `logs/<timestamp>/<container>/`:
- `cpu-build-only.log` or `cpu-load-test.log` for CPU cells
- `gpu-build-only.log` or `gpu-load-test.log` for GPU cells

## Troubleshooting

See `scripts/ci-local/README.md` for:
- KVM not available
- virtiofs mount issues
- Docker pull failures inside the VM
- insmod hangs and timeout behavior

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | All stages passed |
| 1-4 | KVM preflight failure |
| 10 | Host dependency install failed |
| 20-25 | VM provisioning failed |
| 30 | Hang reproduced (diagnostics captured) |
| 40+ | CPU matrix failure |
| 50+ | GPU matrix failure |
