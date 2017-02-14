
int
foo(int c, int a, int b) {
  unsigned buffer[20] = {
          0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  };
  if (c) {
    a *= -2;
    b += 2;
  } else {
    a *= 3;
    b -= 1;
  }
  a = 20 + a + b;
  return buffer[a];
}


int
main(int argc, char **argv) {
  int x = 1;
  int y = 2;
  return foo(argc, x, y);
}

