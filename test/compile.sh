#!/bin/bash
CLANGPATH=/Users/cmk/llvm/bin/clang-3.9
for f in c/*.c; 
do
	b=${f##*/}
	filename=${b%%.*}
	$CLANGPATH -g -c -emit-llvm c/$filename.c -o ll/$filename.bc
done
