#!/usr/bin/env bash
#
# DKMS POST_BUILD guard for datadev-gpu-dkms.
#
# PRE_BUILD (build-nvidia.sh) checks that NVIDIA is available; this checks that
# GPU support actually ended up in the module.  Those are different questions,
# and the second one has been answered wrongly in the past without anything
# noticing: NVIDIA was present, PRE_BUILD succeeded, and the build still produced
# a module without DATA_GPU because NVIDIA_DRIVERS never reached the kbuild pass.
# The package was then installed as datadev-gpu-dkms regardless, and only
# 'GPUAsync Support' in /proc/datadev_* revealed it.
#
# The two branches of the #ifdef DATA_GPU in dma_common.c compile a different
# string literal into the module, so the built object can be interrogated
# directly, with no NVIDIA hardware and without loading anything.
#
# Run from the DKMS build directory, where PRE_BUILD left Makefile.local.

set -eu

MODULE=${1:-datadev.ko}

# Makefile.local is how PRE_BUILD reports what it decided: NVIDIA_DRIVERS=<path>
# when a matching NVIDIA tree was found, and empty when it was told to carry on
# without one (ALLOW_NO_NVIDIA=1, as CI does).  Taking the intent from there
# rather than from the environment keeps this consistent with whatever PRE_BUILD
# actually did, however dkms was invoked.
if [ ! -s Makefile.local ]; then
    echo "--> Built without NVIDIA by request; skipping the GPU support check."
    exit 0
fi

# On distributions that install compressed modules -- Rocky 9 among them -- dkms
# has already compressed the module by the time POST_BUILD runs, so the plain .ko
# is gone.  Accept either form.
FOUND=
for CANDIDATE in "$MODULE" "$MODULE".xz "$MODULE".gz "$MODULE".zst; do
    if [ -f "$CANDIDATE" ]; then
        FOUND=$CANDIDATE
        break
    fi
done

if [ -z "$FOUND" ]; then
    echo "ERROR: none of $MODULE{,.xz,.gz,.zst} found in $PWD;" >&2
    echo "       cannot verify that GPU support was built in."  >&2
    exit 1
fi

case "$FOUND" in
    *.xz)  READER="xz -dc"   ;;
    *.gz)  READER="gzip -dc" ;;
    *.zst) READER="zstd -dc" ;;
    *)     READER="cat"      ;;
esac

if $READER "$FOUND" | grep -aq 'GPUAsync Support : Enabled'; then
    echo "--> Verified: $FOUND was built with DATA_GPU."
    exit 0
fi

echo "ERROR: $FOUND was built WITHOUT DATA_GPU, even though PRE_BUILD found"  >&2
echo "       NVIDIA at $(sed -n 's/^NVIDIA_DRIVERS=//p' Makefile.local)."      >&2
echo "       Installing it would give a module that cannot do GPU DMA while"   >&2
echo "       being named datadev-gpu-dkms, which lsmod and modinfo cannot"     >&2
echo "       distinguish from a working one.  Refusing to continue."           >&2
exit 1
