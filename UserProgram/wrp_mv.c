#include <stdio.h>

int mv_simple(const char *oldpath, const char *newpath) {
	return rename(oldpath, newpath);
}

int mv(const char *oldpath, const char *newpath) {
	FILE *oldptr;
	FILE *newptr;

	oldptr = fopen(oldpath, "r");
	if (oldptr == NULL) {
		printf("%s does not exist\n", oldpath);
		return 1;
	} 

	newptr = fopen(newpath, "r");
	if (newptr != NULL) {
		printf("%s already exists", newpath);
		return 1;
	}

	newptr = fopen(newpath, "wb");
	if (newptr == NULL) {
		printf("%s cannot be created\n", newpath);
		return 1;
	}

	char c;
	while ( (c = fgetc(oldptr)) != EOF ) {
		fputc(c, newptr);
	}

	fclose(oldptr);
	fclose(newptr);
	remove(oldpath);

	return 0;
}

int main (int argc, char **argv)
{
	if (argc != 3) {
		printf("usage: wrp_mv /path/to/file/source /path/to/file/destination\n");
		return 0;
	}

	if (mv(argv[1], argv[2]) == 0) {
		printf("moved %s to %s\n", argv[1], argv[2]);
	} else {
		printf("failed to move\n");
	}

	return 0;
}