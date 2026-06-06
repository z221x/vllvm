../../../install/bin/clang -S -emit-llvm -O0 ./test_indirectcall.c -o ./test_indirectcall.ll #生成IR文件
../../../install/bin/opt -S -load-pass-plugin=../../build/vollvm.so -passes="indirect-call" ./test_indirectcall.ll -o ./test_indirectcall_pass.ll #使用自定义的Pass处理IR文件
../../../install/bin/clang++ ./test_indirectcall_pass.ll -o test_indirectcall #编译生成可执行文件
./test_indirectcall #运行可执行文件