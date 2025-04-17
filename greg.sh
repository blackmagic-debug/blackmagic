rm -rf build
meson setup build --cross-file cross-file/native-uncommon.ini
meson configure build -Dtargets=efm -Drtt_support=false
meson compile -C build
