#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MARKER "\n__DOWNPLAY_PAYLOAD_V1__\n"
#define PATHBUF 4096

static int mkdirs(const char *path)
{
	char tmp[PATHBUF];
	char *p;

	snprintf(tmp, sizeof(tmp), "%s", path);
	for (p = tmp + 1; *p; p++)
	{
		if (*p == '/')
		{
			*p = '\0';
			mkdir(tmp, 0755);
			*p = '/';
		}
	}
	return (mkdir(tmp, 0755) == 0 || errno == EEXIST ? 0 : -1);
}

static int safe_name(const char *name)
{
	return (name[0] && name[0] != '/' && !strstr(name, "../")
		&& strcmp(name, "..") != 0);
}

static long octal(const char *s, int n)
{
	long v;
	int i;

	v = 0;
	i = 0;
	while (i < n && (s[i] == ' ' || s[i] == '\0'))
		i++;
	while (i < n && s[i] >= '0' && s[i] <= '7')
	{
		v = (v * 8) + (s[i] - '0');
		i++;
	}
	return (v);
}

static int skip_padding(FILE *fp, long size)
{
	long pad;

	pad = (512 - (size % 512)) % 512;
	if (pad && fseek(fp, pad, SEEK_CUR) != 0)
		return (-1);
	return (0);
}

static void parent_dir(char *path)
{
	char *s;

	s = strrchr(path, '/');
	if (s)
	{
		*s = '\0';
		mkdirs(path);
		*s = '/';
	}
}

static long find_payload(FILE *fp)
{
	const char *m;
	size_t ml;
	size_t pos;
	long found;
	int c;

	m = MARKER;
	ml = strlen(m);
	pos = 0;
	found = -1;
	rewind(fp);
	while ((c = fgetc(fp)) != EOF)
	{
		if ((char)c == m[pos])
		{
			pos++;
			if (pos == ml)
			{
				found = ftell(fp);
				pos = 0;
			}
		}
		else
			pos = ((char)c == m[0]) ? 1 : 0;
	}
	return (found);
}

static int read_entry_name(unsigned char *h, char *name, size_t size,
	char *longname)
{
	char raw_name[101];
	char prefix[156];

	if (longname[0])
	{
		snprintf(name, size, "%s", longname);
		longname[0] = '\0';
		return (0);
	}
	memcpy(raw_name, h, 100);
	raw_name[100] = '\0';
	memcpy(prefix, h + 345, 155);
	prefix[155] = '\0';
	if (prefix[0])
		snprintf(name, size, "%s/%s", prefix, raw_name);
	else
		snprintf(name, size, "%s", raw_name);
	return (0);
}

static int read_longname(FILE *fp, long size, char *longname, size_t namesize)
{
	long left;
	size_t want;

	left = size;
	if (left <= 0 || (size_t)left >= namesize)
		return (-1);
	want = (size_t)left;
	if (fread(longname, 1, want, fp) != want)
		return (-1);
	longname[want] = '\0';
	if (want > 0 && longname[want - 1] == '\0')
		longname[want - 1] = '\0';
	return (skip_padding(fp, size));
}

static int extract(FILE *fp, const char *root)
{
	unsigned char h[512];
	char longname[PATHBUF];
	char name[PATHBUF];

	longname[0] = '\0';
	for (;;)
	{
		size_t n;
		int empty;
		long size;
		long mode;
		char type;
		char out[PATHBUF];

		n = fread(h, 1, 512, fp);
		if (n != 512)
			return (-1);
		empty = 1;
		for (int i = 0; i < 512; i++)
		{
			if (h[i])
			{
				empty = 0;
				break;
			}
		}
		if (empty)
			return (0);
		size = octal((char *)h + 124, 12);
		mode = octal((char *)h + 100, 8);
		type = h[156];
		if (type == 'L')
		{
			if (read_longname(fp, size, longname, sizeof(longname)) != 0)
				return (-1);
			continue;
		}
		read_entry_name(h, name, sizeof(name), longname);
		while (name[0] == '.' && name[1] == '/')
			memmove(name, name + 2, strlen(name + 2) + 1);
		if (!name[0])
			continue;
		if (!safe_name(name))
			return (-1);
		snprintf(out, sizeof(out), "%s/%s", root, name);
		if (type == '5')
		{
			if (mkdirs(out) != 0)
				return (-1);
			continue;
		}
		parent_dir(out);
		FILE *w = fopen(out, "wb");
		if (!w)
			return (-1);
		long left = size;
		while (left > 0)
		{
			char buf[8192];
			size_t want;

			want = left > (long)sizeof(buf) ? sizeof(buf) : (size_t)left;
			if (fread(buf, 1, want, fp) != want)
			{
				fclose(w);
				return (-1);
			}
			if (fwrite(buf, 1, want, w) != want)
			{
				fclose(w);
				return (-1);
			}
			left -= want;
		}
		fclose(w);
		chmod(out, mode ? (mode_t)mode : 0755);
		if (skip_padding(fp, size) != 0)
			return (-1);
	}
}

int main(int argc, char **argv)
{
	char exe[PATHBUF];
	char root[PATHBUF];
	char bin[PATHBUF];
	char **child;
	FILE *fp;
	ssize_t n;
	long off;

	n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n < 0)
	{
		perror("readlink");
		return (1);
	}
	exe[n] = '\0';
	fp = fopen(exe, "rb");
	if (!fp)
	{
		perror("open self");
		return (1);
	}
	off = find_payload(fp);
	if (off < 0 || fseek(fp, off, SEEK_SET) != 0)
	{
		fprintf(stderr, "downplay: embedded runtime payload missing\n");
		return (1);
	}
	snprintf(root, sizeof(root), "/tmp/downplay-onefile-%ld", (long)getuid());
	mkdirs(root);
	if (extract(fp, root) != 0)
	{
		fprintf(stderr, "downplay: failed to extract embedded runtime\n");
		return (1);
	}
	fclose(fp);
	snprintf(bin, sizeof(bin), "%s/downplay-bin", root);
	child = calloc((size_t)argc + 1, sizeof(char *));
	if (!child)
		return (1);
	child[0] = argv[0];
	for (int i = 1; i < argc; i++)
		child[i] = argv[i];
	execv(bin, child);
	perror("exec downplay");
	return (1);
}
