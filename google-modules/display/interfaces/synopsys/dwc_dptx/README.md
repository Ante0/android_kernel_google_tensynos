# Synopsys DesignWare DisplayPort TX Example Linux Driver

This package contains the Synopsys DesignWare DisplayPort TX Example
Linux driver, demo application, and utilities.

## Building and Loading the Driver

Specify the path to the kernel build directory

```sh
export KDIR = <path-to-kernel-build-dir>
```

Build the driver

```sh
make
```

Load the driver

```sh
cd scripts
./load
```

## Usage

Once the driver is loaded, it will wait for an HPD. Upon receiving a
HOTPLUG interrupt it will initiate the DP bringup and link training
sequence. Once the link training finishes it will send the default
video pattern.

## DEBUGFS Interface

During driver operation, the driver state can be queried and changed
via the debugfs interface. This interface is located at:
```sh
/sys/kernel/debug/dwc_dptx.X.auto
```

See the documentation in debugfs.c for more information
