/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/8/26
 *
 * libcurl wrapper
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifndef _MSC_VER
#ifdef LIBHP_WITH_CURL

#include "hp_curl.h"
#include "hp_epoll.h"    /* hp_epoll */
#include "hp_timerfd.h"  /* hp_timerfd */
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>       /* errno */
#include <curl/curl.h>   /* libcurl */
#include "sds/sds.h"     /* sds */
#include "libyuarel/yuarel.h"   /* yuarel_parse_query */
#include "c-vector/cvector.h"

/////////////////////////////////////////////////////////////////////////////////////////

/* used only in easy mode */
struct hp_curl_recv {
	sds buf;
};

struct hp_curlm_easy {
	CURL *           handle;
	curl_socket_t    fd;
	hp_epolld        ed;
	sds              buf;

	int (* on_done)(hp_curlm * curlm, char const * url, sds str, void * arg);
	void *           arg;

	hp_curlm *      curlm;  /* ref to context */
};

struct hp_curlitem {
	sds 			 url;
	struct curl_slist * hdrs;
	sds 			 body;
	int (* on_done)(hp_curlm * curlm, char const * url, sds str, void * arg);
	void * 			 arg;
};

static size_t easy_write_data(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	struct hp_curl_recv * data = (struct hp_curl_recv *)userdata;
	data->buf = sdscatlen(data->buf, ptr, size * nmemb);
	return size * nmemb;
}

static size_t multi_write_data(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	struct hp_curlm_easy * citem = (struct hp_curlm_easy *)userdata;
	citem->buf = sdscatlen(citem->buf, ptr, size * nmemb);
	return size * nmemb;
}

sds hp_curl_easy_perform(char const * url, struct curl_slist * hdrs, void * data, int f)
{
	struct hp_curl_recv r;
	r.buf = sdsempty();

	CURL *curl;

	/* init the curl session */
	curl = curl_easy_init();

	/* set URL to get */
	curl_easy_setopt(curl, CURLOPT_URL, url);

	/* no progress meter please */
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);

	/* send all data to this function  */
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, easy_write_data);

	/* we want the body be written to this  */
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r);

	/* timeout */
	curl_easy_setopt( curl, CURLOPT_TIMEOUT, 60);
	/* headers */
	if(hdrs)
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
	//Do we need to add the POST parameters?
	if(data){
		curl_easy_setopt(curl, CURLOPT_POST, 1);
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
		if (f == 0) {
			char const * params = data;
			curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, params); //Copy them just incase
		}
		/* body */
		else if(f == 1) {
			char const * body = data;
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(body));
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
		}
		else if(f == 2){
			struct curl_httppost *form = data;
#if (LIBCURL_VERSION_MAJOR >=7 && LIBCURL_VERSION_MINOR >= 56)
			curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
#else
			curl_easy_setopt(curl, CURLOPT_HTTPPOST, form);
#endif /* LIBCURL_VERSION_MINOR */
		}
	}

	/* get it! */
	curl_easy_perform(curl);
	/* cleanup curl stuff */
	curl_easy_cleanup(curl);

	return r.buf;
}

int hp_curl_append_header_from_qeury_str(struct curl_slist ** hdr, char * querystr)
{
	if(!(hdr && querystr))
		return -1;

	char hdrstr[2048];
	struct yuarel_param yparams[256];

	int n = yuarel_parse_query(querystr, '&', yparams, sizeof(yparams) / sizeof(yparams[0]));

	int i;
	for(i = 0; i < n; ++i) {
		hdrstr[0] = '\0';
		snprintf(hdrstr, sizeof(hdrstr), "%s:%s", yparams[i].key, yparams[i].val);
		*hdr = curl_slist_append(*hdr, hdrstr);
	}
	return n > 0? n : 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

static void check_multi_info(hp_curlm * curlm)
{
	char *done_url;
	CURLMsg *message;
	int pending;
	struct hp_curlm_easy * citem = 0;

	while ((message = curl_multi_info_read(curlm->curl_handle, &pending))) {
		switch (message->msg) {
		case CURLMSG_DONE:
			curl_easy_getinfo(message->easy_handle, CURLINFO_EFFECTIVE_URL, &done_url);
			curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &citem);

			--curlm->n;
			assert(curlm->n >= 0);

			if(citem->on_done)
				citem->on_done(curlm, done_url, citem->buf, citem->arg);
			else{
				sdsfree(citem->buf);
				free(citem);
			}

			curl_multi_remove_handle(curlm->curl_handle, message->easy_handle);
			curl_easy_cleanup(message->easy_handle);

			if(curlm->n == 0)
				hp_timerfd_reset(&curlm->timer, 0);
			break;
		default:
			fprintf(stderr, "%s: CURLMSG default\n", __FUNCTION__);
			break;
		}
	}
}

static int hp_curl_multi_handle_io(struct epoll_event * ev)
{
	assert(ev && hp_epoll_arg(ev));

	struct hp_curlm_easy * citem = (struct hp_curlm_easy *)hp_epoll_arg(ev);
	assert(citem);
	assert(citem->curlm);

	int flags = 0;
	if(ev->events & EPOLLERR){
		flags |= CURL_CSELECT_ERR;
	}
	else{
		if (ev->events & EPOLLIN)
			flags |= CURL_CSELECT_IN;
		if (ev->events & EPOLLOUT)
			flags |= CURL_CSELECT_OUT;
	}

	int running_handles;
	curl_multi_socket_action(citem->curlm->curl_handle, citem->fd, flags, &running_handles);

	check_multi_info(citem->curlm);
	return 0;
}

static int handle_socket(CURL *easy, curl_socket_t s, int action, void *userp,
                  void *socketp)
{
	hp_curlm * curlm = (hp_curlm *) userp;
	assert(curlm);

	struct hp_curlm_easy * citem = 0;
	if (action == CURL_POLL_IN || action == CURL_POLL_OUT || action == CURL_POLL_INOUT) {

		curl_easy_getinfo(easy, CURLINFO_PRIVATE, &citem);
		assert(citem);
		assert(citem->handle == easy);

		citem->fd = s;
		hp_epolld_set(&citem->ed, citem->fd, hp_curl_multi_handle_io, citem);

		curl_multi_assign(curlm->curl_handle, s, (void *) citem);
	}
	switch (action) {
	case CURL_POLL_IN:
		hp_epoll_add(curlm->efds, s, EPOLLIN, &citem->ed);
		break;
	case CURL_POLL_OUT:
		hp_epoll_add(curlm->efds, s, EPOLLOUT, &citem->ed);
		break;
	case CURL_POLL_INOUT:
		hp_epoll_add(curlm->efds, s, EPOLLIN | EPOLLOUT, &citem->ed);
		break;
	case CURL_POLL_REMOVE:
		if (socketp) {
			citem = (struct hp_curlm_easy *) socketp;
			hp_epoll_del(curlm->efds, s, EPOLLIN | EPOLLOUT, &citem->ed);
			curl_multi_assign(curlm->curl_handle, s, 0);
		}
		break;
	default:
		break;
	}

	return 0;
}

int hp_curlm_add(hp_curlm * curlm, const char * url
		, struct curl_slist * hdrs, const char * body
		, int (* on_done)(hp_curlm * curlm, char const * url, sds str, void * arg)
		, void * arg)
{
	if (!(curlm && url))
		return -1;

	CURL * handle = curl_easy_init();
	if (!handle)
		return -1;

	struct hp_curlm_easy * citem = malloc(sizeof(struct hp_curlm_easy));
	citem->handle = handle;
	citem->on_done = on_done;
	citem->arg = arg;
	citem->buf = sdsempty();
	citem->curlm = curlm;

	curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, multi_write_data);
	curl_easy_setopt(handle, CURLOPT_WRITEDATA, citem);
	curl_easy_setopt(handle, CURLOPT_PRIVATE, citem);

	/* headers */
	if(hdrs)
		curl_easy_setopt(handle, CURLOPT_HTTPHEADER, hdrs);
	/* body */
	if(body){
		curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "POST");
		curl_easy_setopt(handle, CURLOPT_COPYPOSTFIELDS, body); //Copy them just incase
	}

	curl_easy_setopt(handle, CURLOPT_URL, url);
	curl_multi_add_handle(curlm->curl_handle, handle);

	hp_timerfd_reset(&curlm->timer, 100);

	++curlm->n;

	return 0;
}

static int hp_curl_multi_handle_timeout(hp_timerfd * timerfd)
{
	assert(timerfd);
	hp_curlm * curlm = (hp_curlm * )timerfd->arg;

	int running_handles;
	curl_multi_socket_action(curlm->curl_handle, CURL_SOCKET_TIMEOUT, 0,
			&running_handles);

	check_multi_info(curlm);
	return 0;
}

int hp_curlm_queue(hp_curlm * curlm, const char * url, struct curl_slist * hdrs, char const * body
		, int (* on_done)(hp_curlm * curlm, char const * url, sds str, void * arg)
		, void * arg)
{
	if(!(curlm && url))
		return -1;

	hp_curlitem * item = calloc(1, sizeof(hp_curlitem));
	item->url = sdsnew(url);
	item->hdrs = hdrs;
	item->body = sdsnew(body);
	item->on_done = on_done;
	item->arg = arg;

	cvector_push_back(curlm->items, item);

	return 0;
}

static int hp_curlm_before_wait(struct hp_epoll * efds, void * arg)
{
	assert(efds && arg);
	hp_curlm * curlm = (hp_curlm *)arg;

	for(; !cvector_empty(curlm->items);){
		if(curlm->n >= curlm->max_n)
			break;

		hp_curlitem * item = curlm->items[0];
		assert(item);

		int rc = hp_curlm_add(curlm, item->url, item->hdrs, item->body, item->on_done, item->arg);
		assert(rc == 0);

		cvector_erase(curlm->items, 0);

		sdsfree(item->url);
		sdsfree(item->body);
		free(item);
	}

	return 0;
}

int hp_curlm_init(hp_curlm * curlm, hp_epoll * efds, int max_n)
{
	if(!(curlm && efds))
		return -1;
	memset(curlm, 0, sizeof(hp_curlm));

	curlm->efds = efds;
	curlm->max_n = (max_n >= 0? max_n : 0);

	if(curl_global_init(CURL_GLOBAL_ALL))
		return -1;

	curlm->curl_handle = curl_multi_init();
	curl_multi_setopt(curlm->curl_handle, CURLMOPT_SOCKETFUNCTION, handle_socket);
	curl_multi_setopt(curlm->curl_handle, CURLMOPT_SOCKETDATA, curlm);

	if(hp_timerfd_init(&curlm->timer, efds, hp_curl_multi_handle_timeout, 0, curlm) != 0)
		return -1;

	cvector_init(curlm->items, curlm->max_n);

	int rc = hp_epoll_add_before_wait(efds, hp_curlm_before_wait, curlm);
	assert(rc == 0);

	return 0;
}

void hp_curlm_uninit(hp_curlm * curlm)
{
	if(!curlm)
		return;

	hp_timerfd_uninit(&curlm->timer);
	curl_multi_cleanup(curlm->curl_handle);

	curl_global_cleanup();

	size_t i;
	for(i = 0; i < cvector_size(curlm->items); ++i){
		hp_curlitem * item = curlm->items[i];
		sdsfree(item->url);
		sdsfree(item->body);
		free(curlm->items[i]);
	}
	cvector_free(curlm->items);
}

/////////////////////////////////////////////////////////////////////////////////////////
/* tests */
#ifndef NDEBUG

#include <unistd.h>
#include <sys/stat.h>	/*fstat*/

#define MULTI_REQ_HTTP "http://mirrors.163.com/centos/filelist.gz"
#define MULTI_REQ_FILE "download/filelist.gz"

static int queue_done = 0;

static int hp_curl_multi_test_on_done2(hp_curlm * curlm, char const * url, sds str, void * arg)
{
	assert(curlm);
	assert(strcmp(url, MULTI_REQ_HTTP) == 0);
	assert(arg == curlm);

	++queue_done;

	if(queue_done == 30)
		curlm->efds->stop = 1;

	return 0;
}

static int hp_curl_multi_test_on_done(hp_curlm * curlm, char const * url, sds str, void * arg)
{
	assert(curlm);
	assert(strcmp(url, MULTI_REQ_HTTP) == 0);
	assert(arg == curlm);

	struct stat fsobj, * fs = &fsobj;
	int rc = stat(MULTI_REQ_FILE, fs);
	assert(rc == 0);

	FILE * f = fopen(MULTI_REQ_FILE, "r");
	assert(f);

	char * buf = malloc(fs->st_size);
	size_t size = fread(buf, sizeof(char), fs->st_size, f);
	assert(size == fs->st_size);
	fclose(f);

	size_t slen = sdslen(str);
	assert(slen == size);

	assert(strncmp(str, buf, size) == 0);
	free(buf);

	sdsfree(str);

	if(curlm->n == 0)
		curlm->efds->stop = 1;

	return 0;
}

int test_hp_curl_main(int argc, char ** argv)
{
	/* easy mode */
	{
		sds buf = 0;

		curl_global_init(CURL_GLOBAL_ALL);
		char const * url = MULTI_REQ_HTTP;
		fprintf(stdout, "%s: request url='%s' ...\n", __FUNCTION__, url);

		struct curl_slist * hdrs = 0;
//		char * defhdrstr = strdup("Content-Type=application/json");
//		hp_curl_append_header_from_qeury_str(&hdrs, defhdrstr);
//		free(defhdrstr);
//
//		hp_curl_append_header_from_qeury_str(&hdrs, defhdrstr);
//		assert(hdrs);

		buf = hp_curl_easy_perform(url, hdrs, 0, 0);

		assert(buf);
		fprintf(stdout, "%s: from url='%s', reponse='%s'\n", __FUNCTION__, url, buf);
		sdsfree(buf);
		curl_global_cleanup();
	}

	/* TODO: update tests */
	return 0;

	{
		curl_global_init(CURL_GLOBAL_ALL);
		char const * url = MULTI_REQ_HTTP;
		fprintf(stdout, "%s: request url='%s' ...\n", __FUNCTION__, url);


		struct curl_slist * hdrs = 0;
		hdrs = curl_slist_append(hdrs, "accessToken:89c36d5cc305482aa082a40fb4c0ac47");

		sds buf = hp_curl_easy_perform(url, hdrs, "deptCode=440300000000", 0);
//		fprintf(stdout, "%s: from url='%s', reponse='%s'\n", __FUNCTION__, url, buf);
		sdsfree(buf);
		curl_global_cleanup();
	}
	{
		char const * url = MULTI_REQ_HTTP;
		curl_global_init(CURL_GLOBAL_ALL);
		fprintf(stdout, "%s: request url='%s' ...\n", __FUNCTION__, url);


		struct curl_slist * hdrs = 0;
		hdrs = curl_slist_append(hdrs, "accessToken:89c36d5cc305482aa082a40fb4c0ac47");

		sds buf = hp_curl_easy_perform(url, hdrs, 0, 0);
		fprintf(stdout, "%s: from url='%s', reponse='%s'\n", __FUNCTION__, url, buf);
		sdsfree(buf);
		curl_global_cleanup();
	}
	/* with huge size json file */
	{
		char const * url = MULTI_REQ_HTTP;
		curl_global_init(CURL_GLOBAL_ALL);
		fprintf(stdout, "%s: request url='%s' ...\n", __FUNCTION__, url);


		struct curl_slist * hdrs = 0;
		hdrs = curl_slist_append(hdrs, "accessToken:89c36d5cc305482aa082a40fb4c0ac47");

		sds str = hp_curl_easy_perform(url, hdrs, 0, 0);

		struct stat fsobj, * fs = &fsobj;
		int rc = stat(MULTI_REQ_FILE, fs);
		assert(rc == 0);

		FILE * f = fopen(MULTI_REQ_FILE, "r");
		assert(f);

		char * buf = malloc(fs->st_size);
		size_t size = fread(buf, sizeof(char), fs->st_size, f);
		assert(size == fs->st_size);
		fclose(f);

		size_t slen = sdslen(str);
		assert(slen == size);

		if(strncmp(str, buf, size) != 0)
			fprintf(stdout, "%s: %s", __FUNCTION__, str);
		assert(strncmp(str, buf, size) == 0);
		free(buf);

		fprintf(stdout, "%s: from url='%s', reponse=%zu\n", __FUNCTION__, url, slen);
		sdsfree(str);

		curl_global_cleanup();
	}
	////////////////////////////////////////////////
	/* multi mode */
	{
		int rc;
		hp_epoll efdsobj, * efds = &efdsobj;
		rc = hp_epoll_init(efds, 200);
		assert(rc == 0);

		hp_curlm hp_curl_multiobj, * curlm = &hp_curl_multiobj;
		rc = hp_curlm_init(curlm, efds, 0);
		assert(rc == 0);

		assert(hp_curlm_add);

		rc = hp_curlm_add(curlm, MULTI_REQ_HTTP, 0, 0, hp_curl_multi_test_on_done, curlm);
		assert(rc == 0);

		rc = hp_curlm_add(curlm, MULTI_REQ_HTTP, 0, 0, hp_curl_multi_test_on_done, curlm);
		assert(rc == 0);

		rc = hp_curlm_add(curlm, MULTI_REQ_HTTP, 0, 0, hp_curl_multi_test_on_done, curlm);
		assert(rc == 0);

		assert(curlm->n == 3);

		hp_epoll_run(efds, 200, 0);

		assert(curlm->n == 0);

		hp_curlm_uninit(curlm);
	}
	////////////////////////////////////////////////
	/* multi mode, with concurrency control */
	{
		int rc;
		hp_epoll efdsobj, * efds = &efdsobj;
		rc = hp_epoll_init(efds, 200);
		assert(rc == 0);

		hp_curlm hp_curl_multiobj, * curlm = &hp_curl_multiobj;
		rc = hp_curlm_init(curlm, efds, 20);
		assert(rc == 0);

		size_t i;
		for(i = 0; i < 30; ++i){
			rc = hp_curlm_queue(curlm, MULTI_REQ_HTTP, 0, 0, hp_curl_multi_test_on_done2, curlm);
			assert(rc == 0);

		}

		hp_epoll_run(efds, 200, 0);

		assert(curlm->n == 0);

		hp_curlm_uninit(curlm);
	}
	return 0;
}

#endif /* NDEBUG */

#endif /* LIBHP_WITH_CURL */
#endif /* _MSC_VER */
