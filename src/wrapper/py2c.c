/* py2c.c - DownPlay Cython build wrapper */

#include "libft.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef _WIN32
# define PSEP "\\"
# define EXE ".exe"
# include <direct.h>
# define mkdirp(path) _mkdir(path)
#else
# define PSEP "/"
# define EXE ""
# define mkdirp(path) mkdir(path, 0755)
#endif

#define BUF 4096
#define BIG 8192
#define QUIET 0
#define NORMAL 1
#define VERBOSE 2
#define TASK_COL 28

#define MODE_DEBUG 0
#define MODE_RELEASE 1

#define RED        "\033[0;31m"
#define GREEN      "\033[0;32m"
#define YELLOW     "\033[0;33m"
#define DIM        "\033[2m"
#define BOLD_RED   "\033[1;31m"
#define BOLD_GREEN "\033[1;32m"
#define NC         "\033[0m"

static char g_root[BUF];
static char g_build[BUF];
static char g_release[BUF];
static char g_venv[BUF];
static int g_verbosity = NORMAL;
static int g_auto_install = 0;
static int g_task_pending = 0;
static struct timeval g_task_start;

static long elapsed_ms(struct timeval *start)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	return ((now.tv_sec - start->tv_sec) * 1000
		+ (now.tv_usec - start->tv_usec) / 1000);
}

static void task_result(const char *name, const char *status, const char *color)
{
	int len;
	int pad;

	if (g_verbosity < NORMAL)
		return;
	if (g_task_pending)
	{
		ft_putstr_fd("\033[A\033[2K\r", 1);
		g_task_pending = 0;
	}
	len = ft_strlen((char *)name);
	ft_putstr_fd("> ", 1);
	ft_putstr_fd((char *)name, 1);
	pad = TASK_COL - len - 2;
	while (pad-- > 0)
		ft_putchar_fd(' ', 1);
	ft_putstr_fd((char *)color, 1);
	ft_putstr_fd((char *)status, 1);
	ft_putstr_fd(NC, 1);
	if (g_task_start.tv_sec != 0)
	{
		long ms = elapsed_ms(&g_task_start);
		char buf[64];
		if (ms >= 1000)
			snprintf(buf, sizeof(buf), " (%lds)", ms / 1000);
		else
			snprintf(buf, sizeof(buf), " (%ldms)", ms);
		ft_putstr_fd(DIM, 1);
		ft_putstr_fd(buf, 1);
		ft_putstr_fd(NC, 1);
	}
	ft_putchar_fd('\n', 1);
}

static void task_running(const char *name)
{
	int len;
	int pad;

	if (g_verbosity < NORMAL)
		return;
	gettimeofday(&g_task_start, NULL);
	len = ft_strlen((char *)name);
	ft_putstr_fd("> ", 1);
	ft_putstr_fd((char *)name, 1);
	pad = TASK_COL - len - 2;
	while (pad-- > 0)
		ft_putchar_fd(' ', 1);
	ft_putstr_fd(DIM "RUNNING" NC "\n", 1);
	g_task_pending = 1;
}

static void task_done(const char *name) { task_result(name, "DONE", GREEN); }
static void task_failed(const char *name) { task_result(name, "FAILED", RED); }
static void task_skipped(const char *name) { task_result(name, "SKIPPED", YELLOW); }
static void task_pass(const char *name) { task_result(name, "PASS", GREEN); }
static void task_fail(const char *name) { task_result(name, "FAIL", RED); }

static void log_error(const char *msg)
{
	ft_putstr_fd(RED "  " NC, 2);
	ft_putendl_fd((char *)msg, 2);
}

static void log_info(const char *msg)
{
	if (g_verbosity < NORMAL)
		return;
	ft_putstr_fd(DIM "  ", 1);
	ft_putendl_fd((char *)msg, 1);
	ft_putstr_fd(NC, 1);
}

static void build_result(int success, long ms)
{
	ft_putchar_fd('\n', 1);
	if (success)
	{
		ft_putstr_fd(BOLD_GREEN "BUILD SUCCESSFUL" NC, 1);
		if (ms > 0)
		{
			char buf[64];
			snprintf(buf, sizeof(buf), " in %lds", ms / 1000);
			ft_putstr_fd(buf, 1);
		}
		ft_putchar_fd('\n', 1);
	}
	else
		ft_putstr_fd(BOLD_RED "BUILD FAILED" NC "\n", 1);
}

static char *path_join(const char *a, const char *b)
{
	char *sep;
	char *full;

	sep = ft_strjoin(a, PSEP);
	full = ft_strjoin(sep, b);
	free(sep);
	return (full);
}

static int run_cmd(const char *cmd)
{
	char *quiet_cmd;
	int ret;

	if (g_verbosity >= VERBOSE)
	{
		ft_putstr_fd("  $ ", 1);
		ft_putendl_fd((char *)cmd, 1);
		return (system(cmd));
	}
	if (ft_strstr((char *)cmd, "<<"))
		return (system(cmd));
	quiet_cmd = ft_strjoin(cmd, " >/dev/null 2>&1");
	ret = system(quiet_cmd);
	free(quiet_cmd);
	return (ret);
}

static int run_capture(const char *cmd, char *out, int outsize)
{
	FILE *fp;
	size_t total;
	char line[1024];
	int ret;
	char *trimmed;

	fp = popen(cmd, "r");
	if (!fp)
		return (-1);
	out[0] = '\0';
	total = 0;
	while (fgets(line, sizeof(line), fp) && total < (size_t)(outsize - 1))
	{
		size_t len = ft_strlen(line);
		if (total + len >= (size_t)outsize)
			len = (size_t)outsize - total - 1;
		ft_memcpy(out + total, line, len);
		total += len;
	}
	out[total] = '\0';
	ret = pclose(fp);
	trimmed = ft_strtrim(out, "\n\r ");
	if (trimmed)
	{
		ft_strlcpy(out, trimmed, outsize);
		free(trimmed);
	}
	if (ret == -1)
		return (-1);
	return (WEXITSTATUS(ret));
}

static int which(const char *cmd)
{
	char buf[BUF];

	snprintf(buf, sizeof(buf), "command -v %s >/dev/null 2>&1", cmd);
	return (system(buf) == 0);
}

static int file_exists(const char *path)
{
	return (access(path, F_OK) == 0);
}

static int dir_exists(const char *path)
{
	struct stat st;

	return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static int mkdirs(const char *path)
{
	char tmp[BUF];
	char *p;

	ft_strlcpy(tmp, path, sizeof(tmp));
	p = tmp + 1;
	while (*p)
	{
		if (*p == '/')
		{
			*p = '\0';
			mkdirp(tmp);
			*p = '/';
		}
		p++;
	}
	mkdirp(tmp);
	return (0);
}

static int rm_recursive(const char *path)
{
	char cmd[BIG];

	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
	return (system(cmd));
}

static char *detect_python(void)
{
	char cmd[BUF];

	if (which("python3"))
		return (ft_strdup("python3"));
	if (which("python"))
	{
		snprintf(cmd, sizeof(cmd),
			"python -c \"import sys; sys.exit(0 if sys.version_info.major == 3 else 1)\"");
		if (system(cmd) == 0)
			return (ft_strdup("python"));
	}
	return (NULL);
}

static char *detect_python_ver(void)
{
	char *py;
	char cmd[BUF];
	char ver[64];

	py = detect_python();
	if (!py)
		return (NULL);
	snprintf(cmd, sizeof(cmd),
		"%s -c \"import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')\"",
		py);
	free(py);
	if (run_capture(cmd, ver, sizeof(ver)) != 0)
		return (NULL);
	return (ft_strdup(ver));
}

static const char *detect_platform(void)
{
#ifdef _WIN32
	return ("windows");
#else
	char buf[64];

	run_capture("uname -s", buf, sizeof(buf));
	if (ft_strncmp(buf, "Linux", 5) == 0)
		return ("linux");
	if (ft_strncmp(buf, "Darwin", 6) == 0)
		return ("macos");
	return ("unknown");
#endif
}

static const char *detect_arch(void)
{
#ifdef _WIN32
	return ("x64");
#else
	char buf[64];

	run_capture("uname -m", buf, sizeof(buf));
	if (ft_strncmp(buf, "arm64", 5) == 0 || ft_strncmp(buf, "aarch64", 7) == 0)
		return ("arm64");
	return ("x64");
#endif
}

static void detect_root(const char *argv0)
{
	char cwd[BUF];
	char *check;
	char *dir;
	char *slash;

	if (getcwd(cwd, sizeof(cwd)))
	{
		check = path_join(cwd, "src/main.py");
		if (file_exists(check))
		{
			ft_strlcpy(g_root, cwd, sizeof(g_root));
			free(check);
			return;
		}
		free(check);
	}
	dir = ft_strdup(argv0);
	if (!dir)
	{
		ft_strlcpy(g_root, ".", sizeof(g_root));
		return;
	}
	slash = ft_strrchr(dir, '/');
	if (slash)
		*slash = '\0';
	else
		ft_strlcpy(dir, ".", BUF);
	check = path_join(dir, "../../src/main.py");
	if (file_exists(check))
	{
		char cmd[BUF];
		snprintf(cmd, sizeof(cmd), "cd '%s/../..' && pwd", dir);
		run_capture(cmd, g_root, sizeof(g_root));
	}
	else
		ft_strlcpy(g_root, ".", sizeof(g_root));
	free(check);
	free(dir);
}

static int install_python_deps(int force)
{
	char cmd[BIG];
	char *pip;
	char *cython;
	char *requirements;
	char *build_requirements;
	int should_install;

	pip = path_join(g_venv, "bin/pip");
	cython = path_join(g_venv, "bin/cython");
	requirements = path_join(g_root, "requirements.txt");
	build_requirements = path_join(g_root, "requirements-build.txt");
	should_install = force || !file_exists(cython);
	if (!should_install)
	{
		free(build_requirements);
		free(requirements);
		free(cython);
		free(pip);
		return (0);
	}
	snprintf(cmd, sizeof(cmd), "'%s' install --upgrade pip wheel setuptools", pip);
	if (run_cmd(cmd) != 0)
	{
		free(build_requirements);
		free(requirements);
		free(cython);
		free(pip);
		return (1);
	}
	snprintf(cmd, sizeof(cmd), "'%s' install --upgrade cython", pip);
	if (run_cmd(cmd) != 0)
	{
		free(build_requirements);
		free(requirements);
		free(cython);
		free(pip);
		return (1);
	}
	if (file_exists(requirements))
		snprintf(cmd, sizeof(cmd), "'%s' install --upgrade -r '%s'", pip, requirements);
	else
		snprintf(cmd, sizeof(cmd), "'%s' install --upgrade yt-dlp", pip);
	if (run_cmd(cmd) != 0)
	{
		free(build_requirements);
		free(requirements);
		free(cython);
		free(pip);
		return (1);
	}
	if (file_exists(build_requirements))
	{
		snprintf(cmd, sizeof(cmd), "'%s' install --upgrade -r '%s'", pip, build_requirements);
		if (run_cmd(cmd) != 0)
		{
			free(build_requirements);
			free(requirements);
			free(cython);
			free(pip);
			return (1);
		}
	}
	free(build_requirements);
	free(requirements);
	free(cython);
	free(pip);
	return (0);
}

static int setup_venv(void)
{
	char cmd[BIG];
	char *py;

	task_running("Virtual environment");
	if (!dir_exists(g_venv))
	{
		py = detect_python();
		if (!py)
		{
			task_failed("Virtual environment");
			log_error("Python 3 not found");
			return (1);
		}
		snprintf(cmd, sizeof(cmd), "%s -m venv '%s'", py, g_venv);
		free(py);
		if (run_cmd(cmd) != 0)
		{
			task_failed("Virtual environment");
			return (1);
		}
	}
	if (install_python_deps(g_auto_install) != 0)
	{
		task_failed("Virtual environment");
		log_error("Failed to install build/runtime dependencies");
		return (1);
	}
	task_done("Virtual environment");
	return (0);
}

static int cmd_setup(void)
{
	int ok;

	ok = 1;
	if (which("gcc") || which("cc"))
		task_done("C compiler");
	else
	{
		task_failed("C compiler");
		ok = 0;
	}
	if (detect_python())
		task_done("Python 3");
	else
	{
		task_failed("Python 3");
		ok = 0;
	}
	if (which("make"))
		task_done("make");
	else
	{
		task_failed("make");
		ok = 0;
	}
	if (setup_venv() != 0)
		ok = 0;
	build_result(ok, 0);
	return (ok ? 0 : 1);
}

static int cmd_install_deps(void)
{
	struct timeval start;

	gettimeofday(&start, NULL);
	g_auto_install = 1;
	if (setup_venv() != 0)
	{
		build_result(0, elapsed_ms(&start));
		return (1);
	}
	build_result(1, elapsed_ms(&start));
	return (0);
}

static int write_wrapper_c(int mode, const char *sitepackages, const char *pyver)
{
	char wrapper_path[BUF];
	FILE *fp;

	snprintf(wrapper_path, sizeof(wrapper_path), "%s/wrapper.c", g_build);
	fp = fopen(wrapper_path, "w");
	if (!fp)
		return (1);
	fprintf(fp, "#include <stdio.h>\n");
	fprintf(fp, "#include <stdlib.h>\n");
	fprintf(fp, "#include <string.h>\n");
	fprintf(fp, "#include <unistd.h>\n");
	fprintf(fp, "int __real_main(int argc, char **argv);\n");
	fprintf(fp, "static void exe_dir(char *out, size_t size, char **argv) {\n");
	fprintf(fp, "  ssize_t n = readlink(\"/proc/self/exe\", out, size - 1);\n");
	fprintf(fp, "  if (n < 0) { strncpy(out, argv[0], size - 1); out[size - 1] = '\\0'; }\n");
	fprintf(fp, "  else out[n] = '\\0';\n");
	fprintf(fp, "  char *slash = strrchr(out, '/');\n");
	fprintf(fp, "  if (slash) *slash = '\\0'; else strcpy(out, \".\");\n");
	fprintf(fp, "}\n");
	fprintf(fp, "int main(int argc, char **argv) {\n");
	fprintf(fp, "  char dir[4096];\n");
	fprintf(fp, "  char path[8192];\n");
	fprintf(fp, "  exe_dir(dir, sizeof(dir), argv);\n");
	if (mode == MODE_RELEASE)
	{
		fprintf(fp, "  setenv(\"PYTHONNOUSERSITE\", \"1\", 1);\n");
		fprintf(fp, "  snprintf(path, sizeof(path), \"%%s/runtime\", dir);\n");
		fprintf(fp, "  setenv(\"PYTHONHOME\", path, 1);\n");
		fprintf(fp,
			"  snprintf(path, sizeof(path), "
			"\"%%s/runtime/lib/python%s:"
			"%%s/runtime/lib/python%s/lib-dynload:"
			"%%s/runtime/lib/python%s/site-packages\", dir, dir, dir);\n",
			pyver, pyver, pyver);
		fprintf(fp, "  setenv(\"PYTHONPATH\", path, 1);\n");
	}
	else
	{
		fprintf(fp, "  setenv(\"DOWNPLAY_DEBUG\", \"1\", 1);\n");
		fprintf(fp, "  setenv(\"PYTHONFAULTHANDLER\", \"1\", 1);\n");
		fprintf(fp, "  setenv(\"PYTHONUNBUFFERED\", \"1\", 1);\n");
		fprintf(fp, "  setenv(\"PYTHONTRACEMALLOC\", \"1\", 1);\n");
		fprintf(fp, "  snprintf(path, sizeof(path), \"%s/src:%s\");\n",
			g_root, sitepackages);
		fprintf(fp, "  setenv(\"PYTHONPATH\", path, 1);\n");
		fprintf(fp, "  fprintf(stderr, \"[py2c-debug] exe=%%s\\n\", argv[0]);\n");
		fprintf(fp, "  fprintf(stderr, \"[py2c-debug] PYTHONPATH=%%s\\n\", path);\n");
	}
	fprintf(fp, "  return __real_main(argc, argv);\n");
	fprintf(fp, "}\n");
	fclose(fp);
	return (0);
}

static int step_cython_embed(int mode)
{
	char cmd[BIG];
	char sitepackages[BUF];
	char *cython;
	char *python;
	char *pyver;

	cython = path_join(g_venv, "bin/cython");
	if (!file_exists(cython))
	{
		free(cython);
		cython = ft_strdup("cython");
	}
	snprintf(cmd, sizeof(cmd), "'%s' '%s/src/main.py' --embed -o '%s/main.c'",
		cython, g_root, g_build);
	free(cython);
	if (run_cmd(cmd) != 0)
		return (1);
	python = path_join(g_venv, "bin/python");
	snprintf(cmd, sizeof(cmd),
		"'%s' -c \"import site; print(site.getsitepackages()[0])\"",
		python);
	free(python);
	if (run_capture(cmd, sitepackages, sizeof(sitepackages)) != 0)
		return (1);
	pyver = detect_python_ver();
	if (!pyver)
		return (1);
	if (write_wrapper_c(mode, sitepackages, pyver) != 0)
	{
		free(pyver);
		return (1);
	}
	free(pyver);
	snprintf(cmd, sizeof(cmd),
		"sed -i '/^int$/N;s/^int\\nmain(/int __real_main(/;"
		"s/^int main(/int __real_main(/;"
		"s/^int wmain(/int __real_main(/' '%s/main.c'",
		g_build);
	return (system(cmd) == 0 ? 0 : 1);
}

static int step_gcc_compile(const char *output_path, int mode)
{
	char *pyver;
	char cmd[BIG];
	char py_config[BUF];
	char cflags[BIG];
	char ldflags[BIG];
	const char *compiler;
	const char *rpath;

	pyver = detect_python_ver();
	if (!pyver)
		return (1);
	snprintf(py_config, sizeof(py_config), "python%s-config", pyver);
	if (!which(py_config) && which("python3-config"))
		ft_strlcpy(py_config, "python3-config", sizeof(py_config));
	if (!which(py_config))
	{
		free(pyver);
		log_error("python3-config missing. Install python3-dev or equivalent.");
		return (1);
	}
	snprintf(cmd, sizeof(cmd), "%s --cflags", py_config);
	run_capture(cmd, cflags, sizeof(cflags));
	snprintf(cmd, sizeof(cmd), "%s --ldflags", py_config);
	run_capture(cmd, ldflags, sizeof(ldflags));
	compiler = which("gcc") ? "gcc" : "cc";
	rpath = "";
	if (ft_strncmp(detect_platform(), "linux", 5) == 0)
		rpath = "-Wl,-rpath,'$ORIGIN'";
	if (mode == MODE_RELEASE)
		snprintf(cmd, sizeof(cmd),
			"%s '%s/main.c' '%s/wrapper.c' %s %s -O3 -DNDEBUG -lpython%s %s -o '%s'",
			compiler, g_build, g_build, cflags, ldflags, pyver, rpath, output_path);
	else
		snprintf(cmd, sizeof(cmd),
			"%s '%s/main.c' '%s/wrapper.c' %s %s -O0 -g3 -DDEBUG -lpython%s %s -o '%s'",
			compiler, g_build, g_build, cflags, ldflags, pyver, rpath, output_path);
	free(pyver);
	return (run_cmd(cmd) == 0 ? 0 : 1);
}

static int step_bundle_python_runtime(const char *release_dir)
{
	char cmd[BIG];
	char *python;
	char *pyver;

	python = path_join(g_venv, "bin/python");
	pyver = detect_python_ver();
	if (!pyver)
	{
		free(python);
		return (1);
	}
	snprintf(cmd, sizeof(cmd),
		"'%s' - <<'PY'\n"
		"import os, shutil, site, sysconfig\n"
		"release = %c%s%c\n"
		"pyver = %c%s%c\n"
		"runtime = os.path.join(release, 'runtime')\n"
		"lib = os.path.join(runtime, 'lib', 'python' + pyver)\n"
		"site_dst = os.path.join(lib, 'site-packages')\n"
		"for path in (runtime, lib, site_dst):\n"
		"    os.makedirs(path, exist_ok=True)\n"
		"stdlib = sysconfig.get_path('stdlib')\n"
		"dynload = os.path.join(stdlib, 'lib-dynload')\n"
		"for name in os.listdir(stdlib):\n"
		"    src = os.path.join(stdlib, name)\n"
		"    dst = os.path.join(lib, name)\n"
		"    if name in {'__pycache__', 'site-packages', 'dist-packages', 'ensurepip', 'idlelib', 'tkinter', 'turtledemo', 'test'}:\n"
		"        continue\n"
		"    if os.path.isdir(src):\n"
		"        shutil.copytree(src, dst, dirs_exist_ok=True, ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))\n"
		"    elif src.endswith(('.py', '.txt', '.pem')):\n"
		"        shutil.copy2(src, dst)\n"
		"if os.path.isdir(dynload):\n"
		"    shutil.copytree(dynload, os.path.join(lib, 'lib-dynload'), dirs_exist_ok=True)\n"
		"src_site = site.getsitepackages()[0]\n"
		"shutil.copytree(src_site, site_dst, dirs_exist_ok=True, ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))\n"
		"PY",
		python, '\'', release_dir, '\'', '\'', pyver, '\'');
	free(python);
	free(pyver);
	return (run_cmd(cmd) == 0 ? 0 : 1);
}

static int step_bundle_libpython(const char *release_dir)
{
	char *pyver;
	char cmd[BUF];
	char libdir[BUF];
	char src[BUF];
	char dst[BUF];

	if (ft_strncmp(detect_platform(), "linux", 5) != 0)
		return (0);
	pyver = detect_python_ver();
	if (!pyver)
		return (1);
	run_capture("python3 -c \"import sysconfig; print(sysconfig.get_config_var('LIBDIR'))\"",
		libdir, sizeof(libdir));
	snprintf(src, sizeof(src), "%s/libpython%s.so.1.0", libdir, pyver);
	snprintf(dst, sizeof(dst), "%s/libpython%s.so.1.0", release_dir, pyver);
	free(pyver);
	if (!file_exists(src))
		return (1);
	snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", src, dst);
	return (run_cmd(cmd) == 0 ? 0 : 1);
}

static int __attribute__((unused)) step_write_onefile_launcher(void)
{
	char path[BUF];
	FILE *fp;

	snprintf(path, sizeof(path), "%s/onefile_launcher.c", g_build);
	fp = fopen(path, "w");
	if (!fp)
		return (1);
	fprintf(fp,
		"#include <errno.h>\n"
		"#include <fcntl.h>\n"
		"#include <limits.h>\n"
		"#include <stdio.h>\n"
		"#include <stdlib.h>\n"
		"#include <string.h>\n"
		"#include <sys/stat.h>\n"
		"#include <sys/types.h>\n"
		"#include <unistd.h>\n"
		"#define MARKER \"\\n__DOWNPLAY_PAYLOAD_V1__\\n\"\n"
		"static int mkdirs(const char *path){char tmp[4096];char *p;snprintf(tmp,sizeof(tmp),\"%%s\",path);for(p=tmp+1;*p;p++){if(*p=='/'){*p='\\0';mkdir(tmp,0755);*p='/';}}return mkdir(tmp,0755)==0||errno==EEXIST?0:-1;}\n"
		"static int safe_name(const char *name){return name[0]&&name[0]!='/'&&!strstr(name,\"../\")&&strcmp(name,\"..\")!=0;}\n"
		"static long octal(const char *s,int n){long v=0;int i=0;while(i<n&&(s[i]==' '||s[i]=='\\0'))i++;while(i<n&&s[i]>='0'&&s[i]<='7'){v=(v*8)+(s[i]-'0');i++;}return v;}\n"
		"static void parent_dir(char *path){char *s=strrchr(path,'/');if(s){*s='\\0';mkdirs(path);*s='/';}}\n"
		"static long find_payload(FILE *fp){const char *m=MARKER;size_t ml=strlen(m),pos=0;long found=-1;int c;rewind(fp);while((c=fgetc(fp))!=EOF){if((char)c==m[pos]){pos++;if(pos==ml){found=ftell(fp);pos=0;}}else pos=((char)c==m[0])?1:0;}return found;}\n"
		"static int extract(FILE *fp,const char *root){unsigned char h[512];for(;;){size_t n=fread(h,1,512,fp);if(n!=512)return -1;int empty=1;for(int i=0;i<512;i++)if(h[i]){empty=0;break;}if(empty)return 0;char name[256];memcpy(name,h,100);name[100]='\\0';if(!safe_name(name))return -1;long size=octal((char*)h+124,12);long mode=octal((char*)h+100,8);char type=h[156];char out[4096];snprintf(out,sizeof(out),\"%%s/%%s\",root,name);if(type=='5'){mkdirs(out);}else{parent_dir(out);FILE *w=fopen(out,\"wb\");if(!w)return -1;char buf[8192];long left=size;while(left>0){size_t want=left>(long)sizeof(buf)?sizeof(buf):(size_t)left;if(fread(buf,1,want,fp)!=want){fclose(w);return -1;}if(fwrite(buf,1,want,w)!=want){fclose(w);return -1;}left-=want;}fclose(w);chmod(out,mode?mode:0755);long pad=(512-(size%%512))%%512;if(pad&&fseek(fp,pad,SEEK_CUR)!=0)return -1;}if(type=='5'){long pad=(512-(size%%512))%%512;if(size&&pad&&fseek(fp,pad,SEEK_CUR)!=0)return -1;}}}\n"
		"int main(int argc,char **argv){char exe[4096];ssize_t n=readlink(\"/proc/self/exe\",exe,sizeof(exe)-1);if(n<0){perror(\"readlink\");return 1;}exe[n]='\\0';FILE *fp=fopen(exe,\"rb\");if(!fp){perror(\"open self\");return 1;}long off=find_payload(fp);if(off<0){fprintf(stderr,\"downplay: embedded runtime payload missing\\n\");return 1;}if(fseek(fp,off,SEEK_SET)!=0){perror(\"seek payload\");return 1;}char root[4096];snprintf(root,sizeof(root),\"/tmp/downplay-onefile-%%ld\",(long)getuid());mkdirs(root);if(extract(fp,root)!=0){fprintf(stderr,\"downplay: failed to extract embedded runtime\\n\");return 1;}fclose(fp);char bin[4096];snprintf(bin,sizeof(bin),\"%%s/downplay-bin\",root);char **child=calloc((size_t)argc+1,sizeof(char*));if(!child)return 1;child[0]=bin;for(int i=1;i<argc;i++)child[i]=argv[i];execv(bin,child);perror(\"exec downplay\");return 1;}\n");
	fclose(fp);
	return (0);
}

static int step_make_onefile_release(const char *payload_dir, const char *final_path)
{
	char cmd[BIG];
	char launcher_c[BUF];
	char launcher_bin[BUF];
	char payload_tar[BUF];
	const char *compiler;

	if (ft_strncmp(detect_platform(), "linux", 5) != 0)
		return (0);
	snprintf(launcher_c, sizeof(launcher_c), "%s/src/wrapper/onefile_launcher.c", g_root);
	snprintf(launcher_bin, sizeof(launcher_bin), "%s/onefile_launcher", g_build);
	snprintf(payload_tar, sizeof(payload_tar), "%s/payload.tar", g_build);
	compiler = which("gcc") ? "gcc" : "cc";
	snprintf(cmd, sizeof(cmd), "%s -O2 -DNDEBUG '%s' -o '%s'",
		compiler, launcher_c, launcher_bin);
	if (run_cmd(cmd) != 0)
		return (1);
	snprintf(cmd, sizeof(cmd), "tar -cf '%s' -C '%s' .",
		payload_tar, payload_dir);
	if (run_cmd(cmd) != 0)
		return (1);
	snprintf(cmd, sizeof(cmd),
		"cp '%s' '%s' && printf '\\n__DOWNPLAY_PAYLOAD_V1__\\n' >> '%s' && cat '%s' >> '%s' && chmod +x '%s'",
		launcher_bin, final_path, final_path, payload_tar, final_path, final_path);
	return (run_cmd(cmd) == 0 ? 0 : 1);
}

static int build_binary(int mode, int use_upx)
{
	struct timeval start;
	char output_name[BUF];
	char payload_dir[BUF];
	char payload_bin[BUF];
	char *output_path;
	int failed;

	gettimeofday(&start, NULL);
	if (setup_venv() != 0)
	{
		build_result(0, elapsed_ms(&start));
		return (1);
	}
	mkdirs(g_build);
	if (mode == MODE_RELEASE)
	{
		rm_recursive(g_release);
		mkdirs(g_release);
	}
	snprintf(output_name, sizeof(output_name), "downplay%s", EXE);
	output_path = path_join(mode == MODE_RELEASE ? g_release : g_build, output_name);
	snprintf(payload_dir, sizeof(payload_dir), "%s/release-payload", g_build);
	snprintf(payload_bin, sizeof(payload_bin), "%s/downplay-bin%s", payload_dir, EXE);
	if (mode == MODE_RELEASE)
	{
		rm_recursive(payload_dir);
		mkdirs(payload_dir);
	}
	failed = 0;
	task_running("Cython --embed");
	if (step_cython_embed(mode) != 0)
	{
		task_failed("Cython --embed");
		failed = 1;
	}
	else
		task_done("Cython --embed");
	if (!failed)
	{
		task_running("Native compile");
		if (step_gcc_compile(mode == MODE_RELEASE ? payload_bin : output_path, mode) != 0)
		{
			task_failed("Native compile");
			failed = 1;
		}
		else
			task_done("Native compile");
	}
	if (!failed && mode == MODE_RELEASE)
	{
		task_running("Bundle Python runtime");
		if (step_bundle_python_runtime(payload_dir) == 0
			&& step_bundle_libpython(payload_dir) == 0)
			task_done("Bundle Python runtime");
		else
		{
			task_failed("Bundle Python runtime");
			failed = 1;
		}
	}
	if (!failed && mode == MODE_RELEASE)
	{
		task_running("Create one-file executable");
		if (step_make_onefile_release(payload_dir, output_path) == 0)
			task_done("Create one-file executable");
		else
		{
			task_failed("Create one-file executable");
			failed = 1;
		}
	}
	if (!failed && mode == MODE_RELEASE && use_upx)
	{
		char cmd[BUF];
		task_running("UPX compress");
		snprintf(cmd, sizeof(cmd), "upx --best --lzma '%s'", output_path);
		if (which("upx") && run_cmd(cmd) == 0)
			task_done("UPX compress");
		else
			task_skipped("UPX compress");
	}
	if (!failed)
	{
		char info[BUF];
		snprintf(info, sizeof(info), "Binary: %s", output_path);
		log_info(info);
	}
	free(output_path);
	build_result(!failed, elapsed_ms(&start));
	return (failed ? 1 : 0);
}

static int cmd_build(void)
{
	return (build_binary(MODE_DEBUG, 0));
}

static int cmd_assemble_debug(void)
{
	return (build_binary(MODE_DEBUG, 0));
}

static int cmd_assemble_release(int argc, char **argv)
{
	int use_upx;
	int i;

	use_upx = 1;
	i = 0;
	while (i < argc)
	{
		if (ft_strncmp(argv[i], "--no-upx", 8) == 0)
			use_upx = 0;
		i++;
	}
	(void)detect_arch;
	return (build_binary(MODE_RELEASE, use_upx));
}

static int cmd_clean(void)
{
	struct timeval start;
	char *path;

	gettimeofday(&start, NULL);
	task_running("Clean build artifacts");
	rm_recursive(g_build);
	rm_recursive(g_release);
	rm_recursive(g_venv);
	path = path_join(g_root, "src/wrapper/libft");
	{
		char cmd[BIG];
		snprintf(cmd, sizeof(cmd), "make -C '%s' fclean", path);
		run_cmd(cmd);
	}
	free(path);
	task_done("Clean build artifacts");
	build_result(1, elapsed_ms(&start));
	return (0);
}

static int check_exists(const char *desc, const char *path)
{
	char *full;

	full = path_join(g_root, path);
	if (!file_exists(full))
	{
		task_fail(desc);
		free(full);
		return (1);
	}
	task_pass(desc);
	free(full);
	return (0);
}

static int check_not_exists(const char *desc, const char *path)
{
	char *full;

	full = path_join(g_root, path);
	if (file_exists(full))
	{
		task_fail(desc);
		free(full);
		return (1);
	}
	task_pass(desc);
	free(full);
	return (0);
}

static int cmd_verify(int argc, char **argv)
{
	int failures;
	int strict;
	int i;

	failures = 0;
	strict = 0;
	i = 0;
	while (i < argc)
	{
		if (ft_strncmp(argv[i], "--strict", 8) == 0)
			strict = 1;
		i++;
	}
	failures += check_exists("Entry point", "src/main.py");
	failures += check_exists("Wrapper source", "src/wrapper/py2c.c");
	failures += check_exists("libft", "src/wrapper/libft/libft.h");
	if (strict)
	{
		failures += check_not_exists("build/", "build");
		failures += check_not_exists("release/", "release");
		failures += check_not_exists(".build-venv/", ".build-venv");
	}
	build_result(failures == 0, 0);
	return (failures > 0 ? 1 : 0);
}

static void usage(void)
{
	ft_putstr_fd(
		"py2c - DownPlay Build Wrapper\n"
		"\n"
		"Usage: ./py2c <command> [options]\n"
		"\n"
		"Commands:\n"
		"  setup                  Check tools and create .build-venv\n"
		"  installDeps            Init venv and install/refresh dependencies\n"
		"  build                  Alias for assembleDebug\n"
		"  assembleDebug          Build transparent debug binary in build/downplay\n"
		"  clean                  Remove build artifacts\n"
		"  assembleRelease        Build optimized single-file release executable\n"
		"  verify                 Verify wrapper/project structure\n"
		"\n"
		"Global Options:\n"
		"  --auto-install         Force venv init and dependency refresh\n"
		"  --verbose, -v          Show command output\n"
		"  --quiet, -q            Only show build result\n"
		"\n"
		"assembleRelease Options:\n"
		"  --no-upx               Skip UPX compression\n"
		"\n"
		"verify Options:\n"
		"  --strict               Reject generated build artifacts\n",
		1);
}

int main(int argc, char **argv)
{
	int cmd_start;
	int i;
	const char *cmd;
	int sub_argc;
	char **sub_argv;

	cmd_start = 1;
	i = 1;
	while (i < argc)
	{
		if (ft_strncmp(argv[i], "--verbose", 9) == 0
			|| ft_strncmp(argv[i], "-v", 2) == 0)
		{
			g_verbosity = VERBOSE;
			cmd_start++;
		}
		else if (ft_strncmp(argv[i], "--quiet", 7) == 0
			|| ft_strncmp(argv[i], "-q", 2) == 0)
		{
			g_verbosity = QUIET;
			cmd_start++;
		}
		else if (ft_strncmp(argv[i], "--auto-install", 14) == 0)
		{
			g_auto_install = 1;
			cmd_start++;
		}
		else
			break;
		i++;
	}
	if (cmd_start >= argc)
	{
		usage();
		return (1);
	}
	if (ft_strncmp(argv[cmd_start], "--help", 6) == 0
		|| ft_strncmp(argv[cmd_start], "-h", 2) == 0)
	{
		usage();
		return (0);
	}
	detect_root(argv[0]);
	snprintf(g_build, sizeof(g_build), "%s/build", g_root);
	snprintf(g_release, sizeof(g_release), "%s/release", g_root);
	snprintf(g_venv, sizeof(g_venv), "%s/.build-venv", g_root);
	cmd = argv[cmd_start];
	sub_argc = argc - cmd_start - 1;
	sub_argv = sub_argc > 0 ? argv + cmd_start + 1 : NULL;
	if (ft_strncmp(cmd, "setup", 5) == 0)
		return (cmd_setup());
	if (ft_strncmp(cmd, "installDeps", 11) == 0)
		return (cmd_install_deps());
	if (ft_strncmp(cmd, "build", 5) == 0)
		return (cmd_build());
	if (ft_strncmp(cmd, "assembleDebug", 13) == 0)
		return (cmd_assemble_debug());
	if (ft_strncmp(cmd, "clean", 5) == 0)
		return (cmd_clean());
	if (ft_strncmp(cmd, "assembleRelease", 15) == 0)
		return (cmd_assemble_release(sub_argc, sub_argv));
	if (ft_strncmp(cmd, "verify", 6) == 0)
		return (cmd_verify(sub_argc, sub_argv));
	ft_putstr_fd(BOLD_RED "BUILD FAILED" NC "\n", 2);
	ft_putstr_fd("  Unknown command: ", 2);
	ft_putendl_fd((char *)cmd, 2);
	return (1);
}
