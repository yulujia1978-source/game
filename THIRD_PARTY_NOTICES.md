# Third-party notices

PS5 Turbo is a GPL-3.0-or-later derivative of GyroAim by jmelovich:
https://github.com/jmelovich/ps5-gyroaim
Pinned upstream commit: 3c62ed7041cdc6e113a1c6fcd523b3a1c146ea58.

The process discovery, libScePad import-slot discovery, kstuff remote-syscall
bridge, injection, log, and deploy tooling are retained/adapted from that
project. The gyro transform, native panel and portal are removed. New code
implements hold-to-turbo, configuration, an independent settings HTTP server/UI and tests. Original SPDX notices and
GPL license are retained. Internal ga_* names identify the reused backend.

The kstuff bridge follows PoorDS4 by ItsBlurf (GPL-3.0-or-later), as credited
in the upstream kstuff.c. Upstream also credits Ghostcontrol by StonedModder
for the import-slot redirection approach.

Built against ps5-payload-dev SDK v0.42 by John Törnblom and contributors:
https://github.com/ps5-payload-dev/sdk
GPL-3.0-or-later except the SDK's separately marked BSD headers.
SDK archive SHA-256: 8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da.
The SDK runtime is linked into the ELF; obtain its corresponding source at:
https://github.com/ps5-payload-dev/sdk/tree/v0.42

No Sony firmware, private keys, or system modules are included.
