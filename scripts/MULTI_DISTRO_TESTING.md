# Multi-Distro Testing

Test driver compilation and loading across all CI matrix distros locally.

## Quick Start

```bash
# Run the full 5-distro CPU + GPU matrix
./scripts/run_ci_parity.sh --matrix
```

This runs every cell from `.github/workflows/ci_pipeline.yml` inside Docker
containers on the parity VM — same images, same scripts, same kernel.

## How It Works

The parity harness provisions a KVM guest with an Azure-flavor kernel
matching the GitHub Actions runners. Each matrix cell runs in a Docker
container inside that guest:

1. **ubuntu:24.04**: build-only and load-test cells (CPU and GPU)
2. **ubuntu:22.04**: load-test (CPU), build-only (GPU)
3. **rockylinux:9**: load-test (CPU), build-only (GPU)
4. **debian:experimental**: load-test (CPU and GPU)
5. **fedora:rawhide**: load-test (CPU and GPU)

The pgpcard matrix runs the same five containers, all build-only.

The load flag is set per cell in `scripts/ci-local/run_matrix.sh`: each
`CPU_CELLS`, `GPU_CELLS` and `PGP_CELLS` entry ends in `|1` (load-test:
build, load, test, unload, DKMS install) or `|0` (build-only: compile plus
dmesg check). This differs from `.github/workflows/ci_pipeline.yml`, where
every cell declares `load_test: true` and the load steps are gated at
runtime on `CI_HOST_MATCH`. A local load-test cell only passes when the
built module matches the parity VM kernel.

## Prerequisites

- Linux host with KVM
- See `scripts/ci-local/README.md` for full prerequisites and setup

## First Run

```bash
# Provision the VM (one-time, ~10 minutes)
./scripts/run_ci_parity.sh --no-hang-repro

# Then run the matrix
./scripts/run_ci_parity.sh --matrix
```

Subsequent runs skip provisioning (idempotent) and go straight to the matrix.

## Interpreting Results

Per-cell logs appear in `logs/<timestamp>/<container>/`. The harness prints
a pass/fail summary table after all cells complete.

Exit code 0 = all cells passed. Non-zero = at least one cell failed.
Cell failures do NOT abort the matrix (fail-fast: false, matching GitHub).

## Adding a New Distro

1. Add the container image to `CPU_CELLS` and `GPU_CELLS` in `scripts/ci-local/run_matrix.sh`
2. Set `load_test=0` (build-only) unless the container runs on the VM's kernel
3. Match the corresponding entry in `.github/workflows/ci_pipeline.yml`

## Troubleshooting

See `scripts/ci-local/README.md` for common issues.
