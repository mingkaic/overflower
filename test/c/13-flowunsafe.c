
int
main(int argc, char **argv) {
	unsigned buffer[20] = {
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	};
	int x = 1;
	int y = 2;
	int a = 0;
	if (argc == 2) {
		x *= -argc;
		y += argc;
	} else if (argc < 2) {
		x *= 3;
		y -= 1;
	} else {
		a = argc;
	}
	int u = buffer[a];
	x = 10 + x + y;
	return buffer[x];
}
