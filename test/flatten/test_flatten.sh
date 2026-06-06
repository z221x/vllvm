../../../install/bin/clang -S -emit-llvm -O0 ./test_flatten.cpp -o ./test_flatten.ll #生成IR文件
../../../install/bin/opt -S -load-pass-plugin=../../build/vollvm.so -passes="flatten-func" ./test_flatten.ll -o ./test_flatten_pass.ll #使用自定义的Pass处理IR文件
../../../install/bin/clang++ ./test_flatten_pass.ll -o test_flatten #编译生成可执行文件
./test_flatten #运行可执行文件