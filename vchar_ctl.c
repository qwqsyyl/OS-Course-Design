#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVICE_PATH "/dev/vchar_device"
#define STATUS_PATH "/sys/class/vchar_device/vchar_device/status"
#define DEFAULT_READ_SIZE 4096U

static void print_usage(const char *program)
{
	printf("Usage:\n");
	printf("  %s read [count]\n", program);
	printf("  %s write <text>\n", program);
	printf("  %s status\n", program);
	printf("  %s shell\n", program);
	printf("\n");
	printf("The shell command provides: open, read, write, seek, close, status, help, quit.\n");
}

static void print_shell_help(void)
{
	puts("Commands:");
	puts("  open [r|w|rw]  call open() and keep the file descriptor");
	puts("  read [count]    call read() on the current file descriptor");
	puts("  write <text>    call write() on the current file descriptor");
	puts("  seek <offset>   call lseek(fd, offset, SEEK_SET)");
	puts("  close           call close() on the current file descriptor");
	puts("  status          display the sysfs device status");
	puts("  help            display this help");
	puts("  quit            close the descriptor if needed and exit");
}

static int parse_nonnegative(const char *text, unsigned long long *value)
{
	char *end;
	unsigned long long parsed;

	if (!text || !*text || *text == '-')
		return -1;

	errno = 0;
	parsed = strtoull(text, &end, 10);
	if (errno || *end != '\0')
		return -1;

	*value = parsed;
	return 0;
}

static int parse_count(const char *text, size_t *count)
{
	unsigned long long parsed;

	if (parse_nonnegative(text, &parsed) < 0 || parsed > SIZE_MAX)
		return -1;

	*count = (size_t)parsed;
	return 0;
}

static int mode_to_flags(const char *mode)
{
	if (!mode || !*mode || strcmp(mode, "rw") == 0)
		return O_RDWR;
	if (strcmp(mode, "r") == 0)
		return O_RDONLY;
	if (strcmp(mode, "w") == 0)
		return O_WRONLY;
	return -1;
}

static int open_device(const char *mode)
{
	int flags = mode_to_flags(mode);
	int fd;

	if (flags < 0) {
		fprintf(stderr, "open: mode must be r, w, or rw\n");
		return -1;
	}

	fd = open(DEVICE_PATH, flags);
	if (fd < 0) {
		perror("open");
		return -1;
	}

	printf("open: fd=%d, device=%s, mode=%s\n", fd, DEVICE_PATH,
	       mode && *mode ? mode : "rw");
	return fd;
}

static int read_fd(int fd, size_t count)
{
	char *buffer;
	ssize_t n;

	buffer = malloc(count ? count : 1);
	if (!buffer) {
		perror("malloc");
		return -1;
	}

	n = read(fd, buffer, count);
	if (n < 0) {
		perror("read");
		free(buffer);
		return -1;
	}

	printf("read: %zd bytes", n);
	if (n > 0) {
		printf(": ");
		fwrite(buffer, 1, (size_t)n, stdout);
	}
	putchar('\n');
	free(buffer);
	return 0;
}

static int write_fd(int fd, const char *text)
{
	size_t length = strlen(text);
	ssize_t n;

	n = write(fd, text, length);
	if (n < 0) {
		perror("write");
		return -1;
	}

	printf("write: %zd of %zu bytes\n", n, length);
	if ((size_t)n != length)
		fprintf(stderr, "write: device accepted only part of the input\n");
	return 0;
}

static int close_fd(int *fd)
{
	int old_fd;

	if (*fd < 0) {
		fprintf(stderr, "close: device is not open\n");
		return -1;
	}

	old_fd = *fd;
	if (close(old_fd) < 0) {
		perror("close");
		return -1;
	}

	*fd = -1;
	printf("close: fd=%d\n", old_fd);
	return 0;
}

static int show_status(void)
{
	char buffer[1024];
	ssize_t n;
	int fd;

	fd = open(STATUS_PATH, O_RDONLY);
	if (fd < 0) {
		perror("status open");
		return -1;
	}

	while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
		if (fwrite(buffer, 1, (size_t)n, stdout) != (size_t)n) {
			fprintf(stderr, "status: failed to write output\n");
			close(fd);
			return -1;
		}
	}

	if (n < 0) {
		perror("status read");
		close(fd);
		return -1;
	}

	if (close(fd) < 0) {
		perror("status close");
		return -1;
	}
	return 0;
}

static char *skip_spaces(char *text)
{
	while (*text == ' ' || *text == '\t')
		text++;
	return text;
}

static void trim_line_end(char *line)
{
	size_t length = strlen(line);

	while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
		line[--length] = '\0';
}

static int run_shell(void)
{
	char *line = NULL;
	size_t capacity = 0;
	int fd = -1;
	int interactive = isatty(STDIN_FILENO);

	if (interactive) {
		puts("vchar command shell; type help for commands");
		printf("vchar> ");
		fflush(stdout);
	}

	while (getline(&line, &capacity, stdin) >= 0) {
		char *command;
		char *argument;
		char *separator;

		trim_line_end(line);
		command = skip_spaces(line);
		if (!*command)
			goto next_prompt;

		separator = command;
		while (*separator && *separator != ' ' && *separator != '\t')
			separator++;
		if (*separator) {
			*separator++ = '\0';
			argument = skip_spaces(separator);
		} else {
			argument = separator;
		}

		if (strcmp(command, "open") == 0) {
			if (fd >= 0)
				fprintf(stderr, "open: device is already open as fd=%d\n", fd);
			else
				fd = open_device(argument);
		} else if (strcmp(command, "read") == 0) {
			size_t count = DEFAULT_READ_SIZE;

			if (fd < 0) {
				fprintf(stderr, "read: run open first\n");
			} else if (*argument && parse_count(argument, &count) < 0) {
				fprintf(stderr, "read: count must be a nonnegative integer\n");
			} else {
				read_fd(fd, count);
			}
		} else if (strcmp(command, "write") == 0) {
			if (fd < 0)
				fprintf(stderr, "write: run open first\n");
			else if (!*argument)
				fprintf(stderr, "write: text is required\n");
			else
				write_fd(fd, argument);
		} else if (strcmp(command, "seek") == 0) {
			unsigned long long offset;
			off_t result;

			if (fd < 0) {
				fprintf(stderr, "seek: run open first\n");
			} else if (parse_nonnegative(argument, &offset) < 0 || offset > LLONG_MAX) {
				fprintf(stderr, "seek: offset must be a nonnegative integer\n");
			} else {
				result = lseek(fd, (off_t)offset, SEEK_SET);
				if (result < 0)
					perror("lseek");
				else
					printf("seek: offset=%lld\n", (long long)result);
			}
		} else if (strcmp(command, "close") == 0) {
			close_fd(&fd);
		} else if (strcmp(command, "status") == 0) {
			show_status();
		} else if (strcmp(command, "help") == 0) {
			print_shell_help();
		} else if (strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0) {
			break;
		} else {
			fprintf(stderr, "unknown command: %s\n", command);
		}

next_prompt:
		if (interactive) {
			printf("vchar> ");
			fflush(stdout);
		}
	}

	free(line);
	if (fd >= 0) {
		puts("exit: closing the open device");
		close_fd(&fd);
	}
	return 0;
}

static char *join_arguments(int argc, char **argv, int first)
{
	size_t length = 1;
	char *text;
	int i;

	for (i = first; i < argc; i++)
		length += strlen(argv[i]) + (i > first ? 1 : 0);

	text = malloc(length);
	if (!text)
		return NULL;

	text[0] = '\0';
	for (i = first; i < argc; i++) {
		if (i > first)
			strcat(text, " ");
		strcat(text, argv[i]);
	}
	return text;
}

int main(int argc, char **argv)
{
	int fd;
	int result;

	if (argc < 2) {
		print_usage(argv[0]);
		return EXIT_FAILURE;
	}

	if (strcmp(argv[1], "shell") == 0) {
		if (argc != 2) {
			print_usage(argv[0]);
			return EXIT_FAILURE;
		}
		return run_shell();
	}

	if (strcmp(argv[1], "read") == 0) {
		size_t count = DEFAULT_READ_SIZE;

		if (argc > 3 || (argc == 3 && parse_count(argv[2], &count) < 0)) {
			fprintf(stderr, "read: count must be a nonnegative integer\n");
			return EXIT_FAILURE;
		}

		fd = open_device("r");
		if (fd < 0)
			return EXIT_FAILURE;
		result = read_fd(fd, count);
		if (close_fd(&fd) < 0)
			result = -1;
		return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (strcmp(argv[1], "write") == 0) {
		char *text;

		if (argc < 3) {
			fprintf(stderr, "write: text is required\n");
			return EXIT_FAILURE;
		}

		text = join_arguments(argc, argv, 2);
		if (!text) {
			perror("malloc");
			return EXIT_FAILURE;
		}
		fd = open_device("w");
		if (fd < 0) {
			free(text);
			return EXIT_FAILURE;
		}
		result = write_fd(fd, text);
		free(text);
		if (close_fd(&fd) < 0)
			result = -1;
		return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (strcmp(argv[1], "status") == 0 && argc == 2)
		return show_status() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;

	if (strcmp(argv[1], "help") == 0 && argc == 2) {
		print_usage(argv[0]);
		return EXIT_SUCCESS;
	}

	print_usage(argv[0]);
	return EXIT_FAILURE;
}
