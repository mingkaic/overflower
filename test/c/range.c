
void foo (int arg) {
	int zaro = 0;
	int c1 = zaro+10; // c1 = {10, 10}
	int c2 = zaro-29; // c2 = {-29, -29}
	int ud; // ud = undef
	if (arg < 9) {
		ud = c1; // ud = {10, 10}
	}
	else {
		ud = c2; // ud = {-29, -29}
	}
	// MEET: ud = {-29, 10}
	int var = arg + c1; // var = top
	c1 = zaro-20; // c1 = {-20, -20}
	int r = c1 * ud; // r = {-200, 480}
	int a = ud + ud; // a = {-58, 20}
}