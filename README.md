# ALSHPP - ALSH PreProcessor

This repo contains the ALSH PreProcessor, which preprocesses the code before it gets parsed.

The ALSH compiler and ALSH proper are supposed to use this code to preprocess the source code.
It can also be compiled & ran on its own, if you want.
- Usage:
```
alshpp [infile] > [outfile]
```
## Features:
- `@define <name> <value>` - simply text-replaces all instances of `name` with `value` in the file it's in
- `@undefine <name>` - simply revert the effect of @define on a given name, but only below the "`@undefine`", in that same file.
- `@include <path>` - simply grab and paste in the contents of the file at `path`
- `@import` - instead of simply pasting in the whole file, just make the functions (and variables marked `!global`) from that file available to use
- `@main` - mark the following (whether right after, or on the next line) function as the main function, the entry point of the program. Has to exist exactly once, if the `@justrunit` directive is _not_ provided.
- `@justrunit` - allow top-level code, eliminating the need for a main function. (code top to bottom)
- `@justcarryon` - when an error occurs, instead of crashing/exiting,  just carry on with execution as is possible
- `@stdlib` - this one has to basically have a big lookup table
- `@noffi` - this one gets rid of FFI calls (simply comment out all function calls with the `c::` or `ffi::` prefixes)
- `!global` - this one has to be followed by a variable declaration, and makes said variable global, meaning it can be used from other scopes!

see [alsh-std/stdlib](alsh-std/stdlib.md) for the standard library summary.

see [alsh-std/raw_list.txt](alsh-std/raw_list.txt) for the list of standard library functions `@stdlib` has to loop through