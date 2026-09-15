#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct nano_line {
	char *text;
};

struct nano_buffer {
	struct nano_line *lines;
	size_t count;
	size_t cap;
	int dirty;
	char *filename;
};

static void
nano_die(const char *message)
{
	perror(message);
	exit(1);
}

static char *
nano_strdup(const char *s)
{
	char *copy = strdup(s);
	if (copy == NULL) {
		nano_die("strdup");
	}
	return copy;
}

static void
nano_ensure_cap(struct nano_buffer *buffer, size_t needed)
{
	size_t next_cap;
	struct nano_line *next;

	if (buffer->cap >= needed) {
		return;
	}
	next_cap = buffer->cap == 0 ? 8 : buffer->cap * 2;
	while (next_cap < needed) {
		next_cap *= 2;
	}
	next = realloc(buffer->lines, next_cap * sizeof(*next));
	if (next == NULL) {
		nano_die("realloc");
	}
	buffer->lines = next;
	buffer->cap = next_cap;
}

static void
nano_append_line(struct nano_buffer *buffer, const char *text)
{
	nano_ensure_cap(buffer, buffer->count + 1);
	buffer->lines[buffer->count].text = nano_strdup(text);
	buffer->count++;
	buffer->dirty = 1;
}

static void
nano_replace_line(struct nano_buffer *buffer, size_t index, const char *text)
{
	if (index >= buffer->count) {
		fprintf(stderr, "line %zu does not exist\n", index + 1);
		return;
	}
	free(buffer->lines[index].text);
	buffer->lines[index].text = nano_strdup(text);
	buffer->dirty = 1;
}

static void
nano_delete_line(struct nano_buffer *buffer, size_t index)
{
	size_t i;

	if (index >= buffer->count) {
		fprintf(stderr, "line %zu does not exist\n", index + 1);
		return;
	}
	free(buffer->lines[index].text);
	for (i = index; i + 1 < buffer->count; ++i) {
		buffer->lines[i] = buffer->lines[i + 1];
	}
	buffer->count--;
	buffer->dirty = 1;
}

static void
nano_print_help(void)
{
	printf("Panthera nano serial fallback\n");
	printf("  plain text  append a new line\n");
	printf("  .list       show current buffer\n");
	printf("  .set N TXT  replace line N with TXT\n");
	printf("  .del N      delete line N\n");
	printf("  .clear      erase all lines\n");
	printf("  .save       write file and exit\n");
	printf("  .quit       exit without saving\n");
	printf("  .help       show this help\n");
}

static void
nano_list(const struct nano_buffer *buffer)
{
	size_t i;

	if (buffer->count == 0) {
		printf("[empty]\n");
		return;
	}
	for (i = 0; i < buffer->count; ++i) {
		printf("%4zu  %s\n", i + 1, buffer->lines[i].text);
	}
}

static void
nano_clear(struct nano_buffer *buffer)
{
	size_t i;

	for (i = 0; i < buffer->count; ++i) {
		free(buffer->lines[i].text);
	}
	buffer->count = 0;
	buffer->dirty = 1;
}

static void
nano_load(struct nano_buffer *buffer, const char *filename)
{
	FILE *fp;
	char *line = NULL;
	size_t cap = 0;
	ssize_t len;

	buffer->filename = nano_strdup(filename);
	fp = fopen(filename, "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			return;
		}
		nano_die("fopen");
	}

	while ((len = getline(&line, &cap, fp)) != -1) {
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
			line[--len] = '\0';
		}
		nano_append_line(buffer, line);
	}
	buffer->dirty = 0;
	free(line);
	fclose(fp);
}

static void
nano_save(const struct nano_buffer *buffer)
{
	FILE *fp;
	size_t i;

	if (buffer->filename == NULL) {
		fprintf(stderr, "no file selected\n");
		return;
	}
	fp = fopen(buffer->filename, "w");
	if (fp == NULL) {
		nano_die("fopen");
	}
	for (i = 0; i < buffer->count; ++i) {
		if (fputs(buffer->lines[i].text, fp) == EOF || fputc('\n', fp) == EOF) {
			fclose(fp);
			nano_die("fputs");
		}
	}
	if (fclose(fp) != 0) {
		nano_die("fclose");
	}
	printf("saved %s (%zu lines)\n", buffer->filename, buffer->count);
}

static int
nano_parse_index(const char *s, size_t *index_out)
{
	char *end = NULL;
	unsigned long value;

	if (s == NULL || *s == '\0') {
		return 0;
	}
	value = strtoul(s, &end, 10);
	if (end == s || *end != '\0' || value == 0) {
		return 0;
	}
	*index_out = (size_t)(value - 1);
	return 1;
}

static int
nano_handle_command(struct nano_buffer *buffer, char *line)
{
	if (strcmp(line, ".help") == 0) {
		nano_print_help();
		return 0;
	}
	if (strcmp(line, ".list") == 0) {
		nano_list(buffer);
		return 0;
	}
	if (strcmp(line, ".clear") == 0) {
		nano_clear(buffer);
		printf("buffer cleared\n");
		return 0;
	}
	if (strcmp(line, ".save") == 0) {
		nano_save(buffer);
		return 1;
	}
	if (strcmp(line, ".quit") == 0) {
		if (buffer->dirty) {
			printf("discarded unsaved changes\n");
		}
		return 1;
	}
	if (strncmp(line, ".del ", 5) == 0) {
		size_t index;
		if (!nano_parse_index(line + 5, &index)) {
			fprintf(stderr, "usage: .del N\n");
			return 0;
		}
		nano_delete_line(buffer, index);
		return 0;
	}
	if (strncmp(line, ".set ", 5) == 0) {
		char *arg = line + 5;
		char *space = strchr(arg, ' ');
		size_t index;
		if (space == NULL) {
			fprintf(stderr, "usage: .set N TEXT\n");
			return 0;
		}
		*space = '\0';
		if (!nano_parse_index(arg, &index)) {
			fprintf(stderr, "usage: .set N TEXT\n");
			return 0;
		}
		nano_replace_line(buffer, index, space + 1);
		return 0;
	}

	fprintf(stderr, "unknown command: %s\n", line);
	return 0;
}

static void
nano_free(struct nano_buffer *buffer)
{
	size_t i;

	for (i = 0; i < buffer->count; ++i) {
		free(buffer->lines[i].text);
	}
	free(buffer->lines);
	free(buffer->filename);
}

int
main(int argc, char **argv)
{
	struct nano_buffer buffer = {0};
	char *line = NULL;
	size_t cap = 0;
	ssize_t len;
	int done = 0;

	if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
		printf("Panthera nano serial fallback 0.1\n");
		return 0;
	}
	if (argc != 2) {
		fprintf(stderr, "usage: nano FILE\n");
		return 1;
	}

	nano_load(&buffer, argv[1]);
	printf("Panthera nano serial fallback editing %s\n", argv[1]);
	printf("Type plain text to append. Commands: .help .list .set .del .clear .save .quit\n");
	if (buffer.count > 0) {
		nano_list(&buffer);
	}

	while (!done) {
		printf("nano> ");
		fflush(stdout);
		len = getline(&line, &cap, stdin);
		if (len == -1) {
			printf("\n");
			break;
		}
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
			line[--len] = '\0';
		}
		if (line[0] == '.') {
			done = nano_handle_command(&buffer, line);
		} else {
			nano_append_line(&buffer, line);
		}
	}

	free(line);
	nano_free(&buffer);
	return 0;
}
