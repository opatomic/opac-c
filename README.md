Opatomic client library for C. Should build for all major OS's. Designed for simplicity.

Only 1 external library dependency: bigint library (libtommath, mbedtls, or GMP).

Builds with bash and GCC:

    cd build && ./build

libopac.a will be created in build/out

Refer to the comments in the build script to see other build commands (ie 32-bit, cross compile, etc)

## Usage Example

see [opacli-c](https://github.com/opatomic/opacli-c) project for example usage

## Licensing

Most of the code included in this project is licensed with the ISC licensing terms
detailed in the __LICENSE__ file. However, some code is from other projects and the
licensing terms for that code are included in the files themselves. For example, the
following files have a different license:
 - src/rbt*
 - deps/*

The licenses are all similar as long as you do not define OPABIGINT_LIB=GMP. If you define this
then you will link with GMP which has a more restrictive license.

## API

TODO: add some API docs

refer to __src/opac.h__ for the bulk of the client functions

## Source code details

### Build Definitions

    OPA_NOTHREADS - define if threading support should be disabled
    OPABIGINT_LIB=GMP - define to use GMP for bigints rather than libtommath. make sure to install
                        required dependency: on Ubuntu, run `sudo apt-get install libgmp3-dev`

### Memory allocations
This library tries to avoid memory allocations as much as possible. However,
some are unavoidable. By default, the standard library functions are used.
To configure different memory allocation functions, define them at compile time:

    -DOPAMALLOC=userMallocFunc -DOPACALLOC=userCallocFunc -DOPAREALLOC=userReallocFunc -DOPAFREE=userFreeFunc

You can also define those same functions when building libtommath:

    -DXMALLOC=userMallocFunc -DXCALLOC=userCallocFunc -DXREALLOC=userReallocFunc -DXFREE=userFreeFunc

### Multi threading details

If you plan to use the client from 1 thread then the library can be built without
thread support. In this case, the function __opacInitMT__ will not be available.

If the code is compiled with multi threading enabled (OPA_NOTHREADS is not defined) and
__opacInitMT__ is called (rather than __opacInit__), then the client can be accessed
simultaneously in the following ways:

Any 1 thread at a time:
 - __opacSendRequests__
 - __opacParseResponses__

Any threads simultaneously:
 - __opacQueueRequest__
 - __opacQueueAsyncRequest__
 - __opacQueueNoResponseRequest__
 - __opacRemovePersistent__

__opacClose__ must only be called by 1 thread when all other threads have stopped accessing the client

### Callbacks

Networking functionality is not included; callbacks are used instead. It is up to you to decide
how you want to handle your connections.

TODO: add details on callbacks

