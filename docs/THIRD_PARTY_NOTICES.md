# Third-party notices and provenance

The repository has no project-level LICENSE file. The earlier blanket MIT
statement was not supported by a license file and has been removed; this audit
does not choose or grant a new license for the author's code.

## ReflectiveDLLInjection loader

These existing files identify Stephen Fewer / Harmony Security (2012) and
contain a three-clause BSD-style redistribution notice:

- `dll_test/ReflectiveLoader/ReflectiveLoader.c`
- `dll_test/ReflectiveLoader/ReflectiveLoader.h`
- `dll_test/ReflectiveLoader/ReflectiveDLLInjection.h`

Their original headers are retained unchanged. The exact upstream revision and
local modifications have not been established. The wrapper
`dll_reflectivedemo.cpp` and prebuilt `reflective_payload.dll` are separate
artifacts; their provenance/build correspondence remains unverified. These
reflective sample artifacts are not built or executed by the P0 CMake/CTest setup.

The original notice follows, copied from the existing header:

```text
Copyright (c) 2012, Stephen Fewer of Harmony Security (www.harmonysecurity.com)
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted
provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of
conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or other materials provided
with the distribution.

    * Neither the name of Harmony Security nor the names of its contributors may be used to
endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

## Other source and binary files

No independent provenance verification was established for `src/` or the other
legacy demos. Their presence in this repository is not evidence that every line
is original. Existing `dll_test/*.exe`, `dll_test/*.dll` and ignored `bin/`
binaries were not used as build verification artifacts or modified in this round.
Resolve project-level licensing and any remaining source attribution before
publishing a new release.
