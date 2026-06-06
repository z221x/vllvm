cmake -G "Ninja" -S src -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cd build
ninja -j8