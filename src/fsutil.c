/*!
 * This file is Part of xhhp project
 * @author: hongjun.liao<docici@126.com> @date 2018/8/27
 *
 * filesystem util
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_DEPRECADTED
#ifdef _MSC_VER
#include <dirent.h>      /* DIR */
#endif /* _MSC_VER */

#include "Win32_Interop.h"
#include "sdsinc.h"
#include <unistd.h>
#include <sys/stat.h>    /* mkdir */
#include <fcntl.h>       /* ioctl */
#include <string.h> 	 /* snprintf */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>       /* errno */
#include <assert.h>      /* define NDEBUG to disable assertion */
#include "hp_fs.h"
#include <uv.h>
#include <sys/stat.h>

#ifndef _MSC_VER
/////////////////////////////////////////////////////////////////////////////////////

/* mkdir -p
 *
 * https://stackoverflow.com/questions/2336242/recursive-mkdir-system-call-on-unix
 * */
void hp_fsutil_mkdir_p(const char *dir)
{
	char tmp[1024];
	tmp[0] = '\0';
	char *p = NULL;
	size_t len;

	snprintf(tmp, sizeof(tmp),"%s",dir);
	len = strlen(tmp);
	if(tmp[len - 1] == '/')
			tmp[len - 1] = 0;
	for(p = tmp + 1; *p; p++)
			if(*p == '/') {
					*p = 0;
					mkdir(tmp, S_IRWXU | S_IRWXG | S_IRWXO);
					*p = '/';
			}
	mkdir(tmp, S_IRWXU | S_IRWXG | S_IRWXO);
}

int hp_fsutil_chown_R(const char *dir, char const * ug)
{
	if(!(dir && dir[0] != '\0' && ug && ug[0] != '\0'))
		return -1;

	int r = 0;
	char cmd[1024], * SYSTEM_CMD = cmd;
	cmd[0] = '\0';

	snprintf(cmd, sizeof(cmd), "chown -R %s %s", ug, dir);
#ifndef NDEBUG
	fprintf(stderr, "%s: system('%s') ...\n", __FUNCTION__, cmd);
#endif /* NDEBUG */

#ifndef XHHP_NO_SHELL_CMD
	r = system(SYSTEM_CMD);
	if(r != 0)
		fprintf(stderr, "%s: system('%s') failed'\n", __FUNCTION__, cmd);
#else
	fprintf(stderr, "%s: WARNING, disabled, will NOT execute command '%s'\n", __FUNCTION__, SYSTEM_CMD);
#endif /* XHHP_NO_SHELL_CMD */
	return r;
}

/////////////////////////////////////////////////////////////////////////////////////
int hp_fsutil_rm_r(const char *path)
{
	DIR *d = opendir(path);
	size_t path_len = strlen(path);
	int r = -1;

	if (d) {
		struct dirent *p;

		r = 0;

		while (!r && (p = readdir(d))) {
			int r2 = -1;
			char *buf;
			size_t len;

			/* Skip the names "." and ".." as we don't want to recurse on them. */
			if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) {
				continue;
			}

			len = path_len + strlen(p->d_name) + 2;
			buf = malloc(len);

			if (buf) {
				struct stat statbuf;

				snprintf(buf, len, "%s/%s", path, p->d_name);

				if (!stat(buf, &statbuf)) {
					if (S_ISDIR(statbuf.st_mode)) {
						r2 = hp_fsutil_rm_r(buf);
					} else {
						r2 = unlink(buf);
					}
				}

				free(buf);
			}

			r = r2;
		}

		closedir(d);
	}

	if (!r) {
		r = rmdir(path);
	}

	return r;
}

/*
 * vsf_XXX is from vsftpd-3.0.3/sysutil.c
 * */

static int
lock_internal(int fd, int lock_type)
{
  struct flock the_lock;
  int retval;
  int saved_errno;
  memset(&the_lock, 0, sizeof(the_lock));
  the_lock.l_type = lock_type;
  the_lock.l_whence = SEEK_SET;
  the_lock.l_start = 0;
  the_lock.l_len = 0;
  do
  {
    retval = fcntl(fd, F_SETLK, &the_lock);
    saved_errno = errno;
//    vsf_sysutil_check_pending_actions(kVSFSysUtilUnknown, 0, 0);
  }
  while (/*retval < 0 && */saved_errno == EINTR);
  return retval;
}

void
vsf_sysutil_unlock_file(int fd)
{
  int retval;
  struct flock the_lock;
  memset(&the_lock, 0, sizeof(the_lock));
  the_lock.l_type = F_UNLCK;
  the_lock.l_whence = SEEK_SET;
  the_lock.l_start = 0;
  the_lock.l_len = 0;
  retval = fcntl(fd, F_SETLK, &the_lock);
  assert(retval == 0 && "fcntl");
}

int
vsf_sysutil_lock_file_write(int fd)
{
  return lock_internal(fd, F_WRLCK);
}

int
vsf_sysutil_lock_file_read(int fd)
{
  return lock_internal(fd, F_RDLCK);
}

#endif /*_MSC_VER*/

#ifdef _MSC_VER
#define S_ISREG(f) (S_IFREG & (f))
#endif /* _MSC_VER */
/////////////////////////////////////////////////////////////////////////////////////////

int hp_foreach_file(uv_loop_t * loop,
		char const * dirstr, char const * posfix, int (* fn)(char const * fname))
{
	int r;
	if(!(dirstr && fn))
		return -1;

	struct stat fsobj, * fs = &fsobj;
	r = stat(dirstr, fs);
	if(r != 0){
		return -1;
	}
	if(S_ISREG(fs->st_mode)){
		//FIXME: posfix
		return fn(dirstr);
	}

	uv_dir_t* dir;
	static uv_fs_t opendir_req;
	static uv_fs_t readdir_req;
	static uv_dirent_t dirents[1];

	/* Fill the req to ensure that required fields are cleaned up. */
	memset(&opendir_req, 0xdb, sizeof(opendir_req));

	/* Testing the synchronous flavor. */
	r = uv_fs_opendir(loop, &opendir_req, dirstr, NULL);
	assert(r == 0);
	assert(opendir_req.fs_type == UV_FS_OPENDIR);
	assert(opendir_req.result == 0);
	assert(opendir_req.ptr != NULL);

	dir = opendir_req.ptr;
	dir->dirents = dirents;
	dir->nentries = sizeof(dirents) / sizeof(dirents[0]);
	uv_fs_req_cleanup(&opendir_req);

	int rc = 0;
	while (uv_fs_readdir(loop, &readdir_req, dir, NULL) != 0) {
		sds dirsub = sdscatfmt(sdsempty(), "%s/%s", dirstr, dirents[0].name);

		if(dirents[0].type == UV_DIRENT_DIR){
			hp_foreach_file(loop, dirsub, posfix, fn);

		}
		else if(dirents[0].type == UV_DIRENT_FILE || dirents[0].type == UV_DIRENT_LINK){

			if(posfix && strlen(posfix)){
				char const * p = strrchr(dirents[0].name, '.');
				if(!(p && strstr(posfix, p + 1)))
					continue;
			}

			if(fn(dirsub) != 0){
				rc = 1;
				goto ret;
			}
		}
	ret:
		uv_fs_req_cleanup(&readdir_req);

		sdsfree(dirsub);

		if(rc)
			break;
	}
	uv_fs_req_cleanup(&readdir_req);

	return rc;
}


/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
static 	int n = 0;
static int json_dir_only_2_file(char const * fname)
{
	assert(fname);
	++n;
	return 0;
}

static int json_dir_only_1_json_file(char const * fname)
{
	assert(fname); ++n; return 0;
}

static int do_nothing(char const * fname) { return 0; }

static int on_file(char const * fname)
{
	assert(fname);
	++n;
	return 0;
}

int test_hp_fs_main(int argc, char ** argv)
{
	int rc;
	uv_loop_t uvloopobj = { 0 }, * uvloop = &uvloopobj;
	rc = uv_loop_init(uvloop);
	assert(rc == 0);

	assert(hp_foreach_file(0, 0, 0, 0) != 0);
	assert(hp_foreach_file(uvloop, 0, 0, 0) != 0);
	assert(hp_foreach_file(uvloop, ".", 0, do_nothing) == 0);

	n = 0;
	rc = hp_foreach_file(uvloop, "json/", "", json_dir_only_2_file); assert(n == 2);

	n = 0;
	rc = hp_foreach_file(uvloop, "json/", "json/JSON", json_dir_only_1_json_file);; assert(n == 1);
	assert(rc == 0);

	n = 0;
	rc = hp_foreach_file(uvloop, "json/2", "", on_file); assert(n == 1);

	//for (; !s_test->err && !s_test->done;) {
	//	uv_run(s_test->uvloop, UV_RUN_NOWAIT);
	//}

	uv_loop_close(uvloop);

#ifndef _MSC_VER

	hp_fsutil_mkdir_p("/tmp/a/b/c/d/");
	FILE * f = fopen("/tmp/a/b/c/d/e", "w");
	assert(f);
	fclose(f);

	hp_fsutil_rm_r("/tmp/a/b/c/d/");
	f = fopen("/tmp/a/b/c/d/e", "r");
	assert(!f);
#endif /* _MSC_VER */

	return 0;
}
#endif /* NDEBUG */
#endif //LIBHP_DEPRECADTED
