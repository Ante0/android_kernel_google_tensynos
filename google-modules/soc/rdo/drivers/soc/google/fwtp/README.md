# Pixel Firmware Tracepoint (FWTP) Drivers

See [Pixel Firmware Tracepoint Drivers Documentation](../../../../Documentation/fwtp/fwtp.rst).

## Building FWTP Drivers Documentation {#build}

The FWTP drivers documentation makes use of the kernel-doc reStructuredText
directive. As such, a lot of the documentation in `fwtp.rst` file is generated
from the sources.

To view the full documentation, it must be built first. A symbolic link must be
added from the kernel tree to the private sources. After that is set up,
building the kernel `htmldocs` target will build the FWTP drivers documentation.

```shell
$ find . -name kernel-doc
./aosp/scripts/kernel-doc
.
.
.
$ ln -sf `pwd`/private aosp/Documentation/private && \
  ln -sf `pwd`/private aosp/private
$ (cd aosp && make htmldocs)
.
.
.
$ echo $(basename $(find aosp/Documentation/output/private -name fwtp.html))
fwtp.html
$ google-chrome $(find aosp/Documentation/output/private -name fwtp.html)
```
