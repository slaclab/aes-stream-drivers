#!/usr/bin/env bash
#
# Build, install and load the datadev DKMS package from this checkout, in one
# command, after a git pull.
#
#   sudo ./dkms-reload.sh          # GPU nodes  (datadev-gpu-dkms)
#   sudo ./dkms-reload.sh cpu      # CPU nodes  (datadev-dkms)
#
# Everything here can be done by hand, but the sequence is long enough that
# retyping it per driver update invites mistakes -- particularly the version
# string, which changes with every commit and must match the tarball rather than
# whatever `git describe` says in your shell.
#
# Deliberately refuses rather than forces when the module is busy: pulling
# datadev out from under a running DAQ is not a convenience.

set -euo pipefail

VARIANT=${1:-gpu}
case "$VARIANT" in
    gpu) PKG=datadev-gpu-dkms ;;
    cpu) PKG=datadev-dkms     ;;
    *)   echo "usage: $0 [gpu|cpu]" >&2; exit 2 ;;
esac

if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: run this with sudo." >&2
    exit 1
fi

cd "$(dirname "$(realpath "$0")")"

# Build the tarball as the invoking user.  Running make as root leaves the
# checkout full of root-owned objects that the owner then cannot clean.
BUILDER=${SUDO_USER:-root}
echo "==> make dkms (as $BUILDER)"
if [ "$BUILDER" != root ]; then
    sudo -u "$BUILDER" make dkms
else
    make dkms
fi

TARBALL=$(ls -t "$PKG"-*.tar.gz 2>/dev/null | head -1)
if [ -z "$TARBALL" ]; then
    echo "ERROR: make dkms produced no $PKG-*.tar.gz" >&2
    exit 1
fi
# From the tarball, not from git describe: the Makefile builds the version with
# the user's git config neutralised, so the two can disagree on abbreviation
# length.  This is the same expression the CI packaging scripts use.
DVER=$(tar xzOf "$TARBALL" dkms_source_tree/dkms.conf | sed -n 's/^PACKAGE_VERSION=//p')
if [ -z "$DVER" ]; then
    echo "ERROR: no PACKAGE_VERSION in $TARBALL" >&2
    exit 1
fi
echo "==> $TARBALL -> $PKG/$DVER"

INUSE=$(lsmod | awk '$1 == "datadev" { print $3 }')
if [ -n "${INUSE:-}" ] && [ "$INUSE" != 0 ]; then
    echo "ERROR: datadev is in use by $INUSE; stop the DAQ first." >&2
    exit 1
fi

echo "==> dkms ldtarball / build / install"
# ldtarball refuses when the version is already registered, which is the normal
# case when re-running after a failure, so skip it then rather than abort.  Same
# version means the same commit and therefore the same source -- unless the tree
# is dirty, where the version cannot distinguish two different working states.
if dkms status -m "$PKG" -v "$DVER" 2>/dev/null | grep -q .; then
    echo "    $PKG/$DVER is already registered; re-using its source tree"
    case "$DVER" in
        *-dirty) echo "    WARNING: version is -dirty, so the registered source may"
                 echo "             differ from this tree.  Commit, or dkms remove -m"
                 echo "             $PKG -v $DVER --all first." ;;
    esac
else
    dkms ldtarball --archive="$TARBALL"
fi
dkms build   -m "$PKG" -v "$DVER"
dkms install -m "$PKG" -v "$DVER" --force

# Retire every other datadev package and version.  They all install the same
# datadev.ko to the same place, so leaving one behind means modprobe can pick up
# a stale module -- including one of the other variant.
dkms status | awk -F'[/,]' '/^datadev(-gpu)?-dkms\//{ print $1"/"$2 }' | sort -u |
while IFS=/ read -r name version; do
    if [ "$name/$version" != "$PKG/$DVER" ]; then
        echo "==> retiring $name/$version"
        dkms remove -m "$name" -v "$version" --all || true
    fi
done

echo "==> reloading the module"
if lsmod | grep -q '^datadev '; then
    modprobe -r datadev            # not unconditional: fails if not loaded
    # Confirm rather than assume.  Something that reloads datadev on its own --
    # a datadev.service, an rc.local, a udev rule -- can put it straight back,
    # and then the modprobe below is a no-op and the old module stays resident.
    if lsmod | grep -q '^datadev '; then
        echo "ERROR: datadev is loaded again immediately after modprobe -r." >&2
        echo "       Something else is loading it; find and stop that first:"  >&2
        echo "         systemctl list-units '*datadev*'"                       >&2
        echo "         grep -rl datadev /etc/rc.d/rc.local /etc/rc.local \\"   >&2
        echo "              /etc/systemd/system /etc/modules-load.d 2>/dev/null" >&2
        exit 1
    fi
fi
modprobe datadev

echo "==> verifying"
RUNNING=$(cat /sys/module/datadev/srcversion)
ONDISK=$(modinfo -F srcversion datadev)
if [ "$RUNNING" != "$ONDISK" ]; then
    echo "ERROR: running module ($RUNNING) is not the one modprobe finds ($ONDISK)." >&2
    echo "       modprobe would load: $(modinfo -n datadev)"                         >&2
    echo "       Modules on this kernel:"                                            >&2
    find "/lib/modules/$(uname -r)" -name 'datadev.ko*' 2>/dev/null | sed 's/^/         /' >&2
    echo "       The running one was loaded from somewhere else, or by something"    >&2
    echo "       that reloaded it.  Candidates:"                                     >&2
    echo "         systemctl list-units '*datadev*'"                                 >&2
    echo "         grep -rl datadev /etc/rc.d/rc.local /etc/rc.local \\"             >&2
    echo "              /etc/systemd/system /etc/modules-load.d 2>/dev/null"         >&2
    exit 1
fi
echo "    module matches what the next boot will load"

if [ "$VARIANT" = gpu ]; then
    if grep -h "GPUAsync Support" /proc/datadev_* | grep -qv Enabled; then
        echo "ERROR: a card reports 'GPUAsync Support : Disabled'; this is not a" >&2
        echo "       GPU-enabled driver." >&2
        exit 1
    fi
    echo "    GPUAsync support enabled on all cards"
    grep -h "GPU Async En" /proc/datadev_* | grep -q ": 1" ||
        echo "    NOTE: no card reports 'GPU Async En : 1'; check the card firmware"
fi

echo "==> devices: $(echo /dev/datadev_*)"
echo "==> done: $PKG/$DVER"
