# Licensing and distribution

Project-owned source, examples, tests, scripts and documentation are licensed
under **LGPL-3.0-or-later**, copyright (C) 2026 Cavanshirpro. This means LGPL
version 3 or, at your option, a later version.

`LICENSE` contains the LGPL terms and incorporated GPL text. `COPYING` also
provides the GPL v3 text separately. `NOTICE` identifies this project's scope;
`THIRD_PARTY.md` identifies independently licensed dependencies and references.
The installed CMake package includes these notices under `share/licenses`.

If you distribute a modified library, preserve notices and provide the applicable
corresponding source under the license's conditions. A linked application has
additional requirements under LGPL section 4: include notices/license texts and
allow users to use a compatible modified library. A suitable replaceable shared
library is one mechanism; static linking may require relinkable application code
and the necessary accompanying materials. Do not assume that linking statically
removes these obligations. Read the complete [license](../LICENSE), particularly
sections 3–4, for the conditions that apply to your distribution.

Release assets for 0.1.0 are source archives. They do not bundle libcurl, TLS
libraries, JSON, zlib or runtime DLLs. If you distribute those dependencies, keep
their own license notices and inspect the licenses of their enabled components.

License text source: the [SPDX license list](https://spdx.org/licenses/LGPL-3.0-or-later.html).
