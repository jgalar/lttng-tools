/*
 * rotation.c
 *
 * Tests suite for LTTng notification API (rotation notifications)
 *
 * Copyright (C) 2017 Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <assert.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static
int create_file(const char *path)
{
	int fd, ret = 0;

	assert(path);
	fd = creat(path, S_IRWXU);
	if (fd < 0) {
		fprintf(stderr, "# Failed to create file: ");
		ret = -1;
		goto end;
	}

	ret = close(fd);
	if (ret) {
		perror("# Failed to close file: ");
		goto end;
	}
end:
	return ret;
}

int main(int argc, const char *argv[])
{
	int ret = 0;
	const char *name;

	if (argc != 3) {
		puts("Usage: rotation SESSION_NAME");
		ret = 1;
		goto error;
	}

	ret = create_file(argv[2]);
	if (ret) {
		goto error;
	}

	puts("ROTATION_ONGOING 6874 local://allo-mon-gros");
	puts("ROTATION_COMPLETED 6874 local://allo-mon-gros");
error:
	return ret;
}

