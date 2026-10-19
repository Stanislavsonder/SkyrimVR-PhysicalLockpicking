# nifgen

Generates the mod's own models in `package/assets`: the solid-colour debug blocks (`bDebugView`), the keyhole target, the assist ring and the pick counter's digit segment.
They are committed, so you only need this after changing `nifgen.cpp`:

```sh
git clone https://github.com/ousnius/nifly
cmake -S tools/nifgen -B build-nifgen -DNIFLY_DIR=$PWD/nifly -DBUILD_TESTING=OFF
cmake --build build-nifgen
build-nifgen/nifgen package/assets
```
