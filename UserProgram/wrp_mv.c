#include <stdio.h>

int mv_simple(const char *oldpath, const char *newpath) {
	return rename(oldpath, newpath);
}


int main (int argc, char **argv)
{
	printf("wrp_mv\n");
	if (argc != 3) {
		printf("usage: wrp_mv /path/to/file/source /path/to/file/destination\n");
	}

	if (mv_simple(argv[1], argv[2]) == 0) {
		printf("moved %s to %s\n", argv[1], argv[2]);
	} else {
		printf("failed to move\n");
	}

	return 0;
}