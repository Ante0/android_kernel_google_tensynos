// SPDX-License-Identifier: GPL-2.0-only
/*
 * BSTE Example IOCTL Helper
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define BSTE_IOC_MAGIC 'b'
#define BSTE_IOC_PING _IO(BSTE_IOC_MAGIC, 1)

int main(int argc, char *argv[])
{
	int fd;
	long ret;
	const char *dev_path = "/dev/bste_example";

	fd = open(dev_path, O_RDONLY);
	if (fd < 0) {
		perror("Failed to open device");
		return 1;
	}

	ret = ioctl(fd, BSTE_IOC_PING);
	if (ret < 0) {
		perror("IOCTL failed");
		close(fd);
		return 2;
	}

	printf("IOCTL returned: %ld\n", ret);
	close(fd);

	if (ret != 42) {
		fprintf(stderr, "Failure: Received unexpected value %ld\n", ret);
		return 3;
	}

	printf("Success: Received expected magic value 42\n");
	return 0;
}
