#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define MARKER "\n__DOWNPLAY_PAYLOAD_V1__\n"
#define PATHBUF 2048

static int mkdirs_w(const wchar_t *path)
{
	wchar_t tmp[PATHBUF];
	wchar_t *p;
	DWORD attr;

	if (!path || !*path)
		return (-1);
	wcsncpy(tmp, path, PATHBUF - 1);
	tmp[PATHBUF - 1] = L'\0';
	for (p = tmp + 1; *p; p++)
	{
		if (*p == L'/' || *p == L'\\')
		{
			*p = L'\0';
			CreateDirectoryW(tmp, NULL);
			*p = L'\\';
		}
	}
	if (CreateDirectoryW(tmp, NULL))
		return (0);
	attr = GetLastError();
	return (attr == ERROR_ALREADY_EXISTS ? 0 : -1);
}

static int utf8_to_wide(const char *src, wchar_t *dst, size_t dstsz)
{
	int n;

	n = MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, (int)dstsz);
	return (n > 0 ? 0 : -1);
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

static void join_utf8(char *out, size_t outsz, const char *a, const char *b)
{
	snprintf(out, outsz, "%s/%s", a, b);
}

static int extract_path_utf8(const char *root, const char *name, wchar_t *out_w)
{
	char tmp[PATHBUF];

	join_utf8(tmp, sizeof(tmp), root, name);
	return (utf8_to_wide(tmp, out_w, PATHBUF));
}

static int read_longname(FILE *fp, long size, char *longname, size_t namesize)
{
	size_t want;

	if (size <= 0 || (size_t)size >= namesize)
		return (-1);
	want = (size_t)size;
	if (fread(longname, 1, want, fp) != want)
		return (-1);
	longname[want] = '\0';
	if (want > 0 && longname[want - 1] == '\0')
		longname[want - 1] = '\0';
	return (skip_padding(fp, size));
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
		if (type == '5')
		{
			wchar_t wpath[PATHBUF];

			if (extract_path_utf8(root, name, wpath) != 0)
				return (-1);
			if (mkdirs_w(wpath) != 0)
				return (-1);
			continue;
		}
		else
		{
			char out_utf8[PATHBUF];
			wchar_t out_w[PATHBUF];
			FILE *w;
			long left;

			join_utf8(out_utf8, sizeof(out_utf8), root, name);
			if (utf8_to_wide(out_utf8, out_w, PATHBUF) != 0)
				return (-1);
			w = _wfopen(out_w, L"wb");
			if (!w)
				return (-1);
			left = size;
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
			(void)mode;
			if (skip_padding(fp, size) != 0)
				return (-1);
		}
	}
}

static void build_command_line(wchar_t *out, size_t outsz, int argc, wchar_t **argv)
{
	size_t pos;

	pos = 0;
	for (int i = 1; i < argc; i++)
	{
		const wchar_t *arg;
		size_t j;

		arg = argv[i];
		if (i > 1 && pos + 1 < outsz)
			out[pos++] = L' ';
		if (pos + 1 < outsz)
			out[pos++] = L'"';
		j = 0;
		while (arg[j] && pos + 2 < outsz)
		{
			if (arg[j] == L'"')
				out[pos++] = L'\\';
			out[pos++] = arg[j++];
		}
		if (pos + 1 < outsz)
			out[pos++] = L'"';
	}
	out[pos] = L'\0';
}

static int delete_tree_w(const wchar_t *root)
{
	wchar_t pattern[PATHBUF];
	WIN32_FIND_DATAW fd;
	HANDLE hfind;

	if (!root || !*root)
		return (-1);
	wsprintfW(pattern, L"%s\\*", root);
	hfind = FindFirstFileW(pattern, &fd);
	if (hfind != INVALID_HANDLE_VALUE)
	{
		do
		{
			wchar_t item[PATHBUF];
			if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
				continue;
			wsprintfW(item, L"%s\\%s", root, fd.cFileName);
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				delete_tree_w(item);
			else
				DeleteFileW(item);
		}
		while (FindNextFileW(hfind, &fd));
		FindClose(hfind);
	}
	RemoveDirectoryW(root);
	return (0);
}

int wmain(int argc, wchar_t **argv)
{
	wchar_t exe[PATHBUF];
	wchar_t temp_base[PATHBUF];
	wchar_t temp_root[PATHBUF];
	wchar_t payload_dir[PATHBUF];
	wchar_t payload_bin[PATHBUF];
	wchar_t payload_dll_dir[PATHBUF];
	wchar_t payload_runtime[PATHBUF];
	wchar_t payload_site[PATHBUF];
	wchar_t path_env[32768];
	wchar_t cmdline[32768];
	wchar_t path_old[32768];
	char root_utf8[PATHBUF];
	FILE *fp;
	long off;
	DWORD n;
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	BOOL ok;

	n = GetModuleFileNameW(NULL, exe, PATHBUF - 1);
	if (n == 0)
		return (1);
	exe[n] = L'\0';
	fp = _wfopen(exe, L"rb");
	if (!fp)
		return (1);
	off = find_payload(fp);
	if (off < 0 || fseek(fp, off, SEEK_SET) != 0)
	{
		fclose(fp);
		return (1);
	}
	GetTempPathW(PATHBUF - 1, temp_base);
	wsprintfW(temp_root, L"%sdownplay-onefile-%lu-%lu", temp_base,
		(unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount64());
	CreateDirectoryW(temp_root, NULL);
	if (WideCharToMultiByte(CP_UTF8, 0, temp_root, -1, root_utf8, PATHBUF, NULL, NULL) == 0)
	{
		fclose(fp);
		return (1);
	}
	if (extract(fp, root_utf8) != 0)
	{
		fclose(fp);
		return (1);
	}
	fclose(fp);
	wsprintfW(payload_dir, L"%s\\", temp_root);
	wsprintfW(payload_bin, L"%sdownplay-bin.exe", payload_dir);
	wsprintfW(payload_runtime, L"%sruntime", payload_dir);
	wsprintfW(payload_dll_dir, L"%sruntime\\DLLs", payload_dir);
	wsprintfW(payload_site, L"%sruntime\\Lib\\site-packages", payload_dir);
	{
		wchar_t envbuf[32768];
		wchar_t oldpath[32768];

		SetEnvironmentVariableW(L"PYTHONNOUSERSITE", L"1");
		SetEnvironmentVariableW(L"PYTHONHOME", payload_runtime);
		wsprintfW(envbuf, L"%s;%s;%s", payload_runtime, payload_dll_dir, payload_site);
		SetEnvironmentVariableW(L"PYTHONPATH", envbuf);
		GetEnvironmentVariableW(L"PATH", oldpath, 32768);
		wsprintfW(path_env, L"%s;%s;%s", payload_runtime, payload_dll_dir, oldpath);
		SetEnvironmentVariableW(L"PATH", path_env);
	}
	build_command_line(cmdline, 32768, argc, argv);
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));
	ok = CreateProcessW(
		payload_bin,
		cmdline,
		NULL,
		NULL,
		TRUE,
		0,
		NULL,
		temp_root,
		&si,
		&pi);
	if (!ok)
	{
		delete_tree_w(temp_root);
		return (1);
	}
	WaitForSingleObject(pi.hProcess, INFINITE);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	delete_tree_w(temp_root);
	return (0);
}
