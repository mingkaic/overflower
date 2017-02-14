#include <stdio.h>

void bar(int x, int y) {}

void foo() { int x = 2; int y = x + 7; bar(x, y); }