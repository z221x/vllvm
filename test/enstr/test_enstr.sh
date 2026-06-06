../../../install/bin/clang -S -emit-llvm -O0 ./test_enstr.c -o ./test_enstr.ll #生成IR文件
../../../install/bin/opt -S -load-pass-plugin=../../build/vollvm.so -passes="en-str" ./test_enstr.ll -o ./test_enstr_pass.ll #使用自定义的Pass处理IR文件
../../../install/bin/clang ./test_enstr_pass.ll -o test_enstr #编译生成可执行文件
./test_enstr #运行可执行文件