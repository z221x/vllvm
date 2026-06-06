../../../install/bin/clang -S -emit-llvm -O0 ./test_indirectbr.c -o ./test_indirectbr.ll #生成IR文件
../../../install/bin/opt -S -load-pass-plugin=../../build/vollvm.so -passes="en-str" ./test_indirectbr.ll -o ./test_indirectbr_pass.ll 
../../../install/bin/opt -S -load-pass-plugin=../../build/vollvm.so -passes="indirect-call flatten-func indirect-br" ./test_indirectbr_pass.ll -o ./test_indirectbr_pass.ll
../../../install/bin/clang++ ./test_indirectbr_pass.ll -o test_indirectbr #编译生成可执行文件
./test_indirectbr #运行可执行文件