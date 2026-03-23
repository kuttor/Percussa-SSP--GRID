# cmake/

## xcSSP.cmake

The ARM cross-compile toolchain file. This is already in your project
from the initial ELAS setup. Copy it here from your existing project:

```
cp ~/Code/ssp_projects/ssp-elastic-audio/cmake/xcSSP.cmake cmake/
```

This file sets up clang to cross-compile for the SSP's ARM Cortex-A17
using the Buildroot SDK sysroot.
