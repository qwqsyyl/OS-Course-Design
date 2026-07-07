#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVICE_PATH "/dev/vchar_device"
#define READ_BUF_SIZE 512

static void die(const char *msg)
{
	perror(msg);
	exit(EXIT_FAILURE);
}

static void read_from_fd(const char *label, int fd)
{
	char buf[READ_BUF_SIZE];
	ssize_t n;

	memset(buf, 0, sizeof(buf));
	n = read(fd, buf, sizeof(buf) - 1);
	if (n < 0)
		die("read");

	printf("[%s] read %zd bytes: %s\n", label, n, buf);
}

int main(int argc, char **argv)
{
	const char *message = "hello from user space to vchar_device\n";
	int fd1;
	int fd2;
	ssize_t n;

	if (argc > 1)
		message = argv[1];

	fd1 = open(DEVICE_PATH, O_RDWR);
	if (fd1 < 0)
		die("open fd1");

	fd2 = open(DEVICE_PATH, O_RDWR);
	if (fd2 < 0)
		die("open fd2");

	printf("opened %s twice to verify independent offsets\n", DEVICE_PATH);

	n = write(fd1, message, strlen(message));
	if (n < 0)
		die("write fd1");
	printf("[fd1] wrote %zd bytes\n", n);

	if (lseek(fd1, 0, SEEK_SET) < 0)
		die("lseek fd1");

	read_from_fd("fd1 after seek", fd1);
	read_from_fd("fd2 first read", fd2);

	if (lseek(fd2, 0, SEEK_SET) < 0)
		die("lseek fd2");
	read_from_fd("fd2 after seek", fd2);

	close(fd1);
	close(fd2);

	printf("try: cat /sys/class/vchar_device/vchar_device/status\n");
	return EXIT_SUCCESS;
}
