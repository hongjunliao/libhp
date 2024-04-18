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

#ifdef LIBHP_WITH_CURL
#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>       /* errno */
#include "hp/hp_curl.h"
#include "hp/hp_log.h"
#include "hp/sdsinc.h"     /* sds */
#include "libyuarel/yuarel.h"   /* yuarel_parse_query */
#include "c-vector/cvector.h"

/////////////////////////////////////////////////////////////////////////////////////////

/* used only in easy mode */
struct hp_curl_recv {
	sds buf;
};

typedef struct hp_curleasy {
	CURL * 		curl;
	sds         url;
#if !((defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H))
	uv_poll_t 	poll_handle;
#endif
	curl_socket_t fd;
	sds      	resp;   /* the response */
	FILE *      f;      /* the response, write to file? */

	/* for progress */
	int 	    bytes;
	int 	    content_length;

	hp_curl_proress_cb_t on_proress;
	hp_curl_done_cb_t on_done;
	void *      arg;

	hp_curl * hcurl;  /* ref to context */

	struct curl_slist * hdrs;
#if (LIBCURL_VERSION_MAJOR >=7 && LIBCURL_VERSION_MINOR >= 56)
	curl_mime *	form;
#else
	struct curl_httppost * form;
#endif /* LIBCURL_VERSION_MINOR */
} hp_curleasy;

static void hp_curleasy_uninit(hp_curleasy * citem)
{
	assert(citem);

#if (LIBCURL_VERSION_MAJOR >=7 && LIBCURL_VERSION_MINOR >= 56)
	/* then cleanup the form */
	if(citem->form)
		curl_mime_free(citem->form);
#else
	/* cleanup the formpost chain */
	if(citem->form)
		curl_formfree(citem->form);
#endif /* LIBCURL_VERSION_MINOR */
	/* free slist */
	if(citem->hdrs)
		curl_slist_free_all(citem->hdrs);

	if(citem->resp)
		sdsfree(citem->resp);
	sdsfree(citem->url);
}


static size_t easy_write_data(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	struct hp_curl_recv * data = (struct hp_curl_recv *)userdata;
	data->buf = sdscatlen(data->buf, ptr, size * nmemb);
	return size * nmemb;
}

static size_t multi_write_data(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	hp_curleasy * citem = (hp_curleasy *)userdata;

	if(citem->f){
		size_t w = fwrite(ptr, size, nmemb, citem->f);
		assert(w == size * nmemb);
	}
	else if(citem->resp)
		citem->resp = sdscatlen(citem->resp, ptr, size * nmemb);

	citem->bytes += size * nmemb;

	/* check the size */
	double cl;
	int res = curl_easy_getinfo(citem->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &cl);
	if (!res && cl > 0) {
		citem->content_length = (int)cl;
	}

	if(citem->on_proress)
		citem->on_proress(citem->bytes, citem->content_length, citem->resp, citem->arg);

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
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl/7.81.0");
	/* no progress meter please */
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);

	/* send all data to this function  */
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, easy_write_data);

	/* we want the body be written to this  */
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r);

	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
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

static void check_multi_info(hp_curl * hcurl)
{
	CURLMsg *message;
	int pending;
	struct hp_curleasy * citem = 0;

	while ((message = curl_multi_info_read(hcurl->curl_handle, &pending))) {
		switch (message->msg) {
		case CURLMSG_DONE:
			curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &citem);

			if(citem->f) fclose(citem->f);
			if(citem->on_done) citem->on_done(hcurl, message->easy_handle, citem->url, citem->resp, citem->arg);
			hp_curleasy_uninit(citem);
			free(citem);

			curl_multi_remove_handle(hcurl->curl_handle, message->easy_handle);
			curl_easy_cleanup(message->easy_handle);
			break;
		default:
			hp_log(stderr, "%s: CURLMSG default\n", __FUNCTION__);
			break;
		}
	}
}

static int hp_curl_multi_handle_io(struct epoll_event * ev,  void * arg)
{
	assert(ev && arg);

	struct hp_curleasy * citem = (struct hp_curleasy *)arg;
	assert(citem);
	assert(citem->hcurl);

//	hp_timerfd_reset(&citem->hcurl->timer, 0);

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
	curl_multi_socket_action(citem->hcurl->curl_handle, citem->fd, flags, &running_handles);

	check_multi_info(citem->hcurl);
	return 0;
}

static int handle_socket(CURL *easy, curl_socket_t s, int action, void *userp,
                  void *socketp)
{
	hp_curl * hcurl = (hp_curl *) userp;
	assert(hcurl);

	struct hp_curleasy * citem = 0;
	if (action == CURL_POLL_IN || action == CURL_POLL_OUT || action == CURL_POLL_INOUT) {

		curl_easy_getinfo(easy, CURLINFO_PRIVATE, &citem);
		assert(citem);
		assert(citem->curl == easy);

		citem->fd = s;
		curl_multi_assign(hcurl->curl_handle, s, (void *) citem);
	}
	switch (action) {
	case CURL_POLL_IN:
		hp_epoll_add(hcurl->loop, s, EPOLLIN, hp_curl_multi_handle_io, 0, citem);
		break;
	case CURL_POLL_OUT:
		hp_epoll_add(hcurl->loop, s, EPOLLOUT, hp_curl_multi_handle_io, 0, citem);
		break;
	case CURL_POLL_INOUT:
		hp_epoll_add(hcurl->loop, s, EPOLLIN | EPOLLOUT, hp_curl_multi_handle_io, 0, citem);
		break;
	case CURL_POLL_REMOVE:
		if (socketp) {
			citem = (struct hp_curleasy *) socketp;
			hp_epoll_rm(hcurl->loop, s);
			curl_multi_assign(hcurl->curl_handle, s, 0);
		}
		break;
	default:
		break;
	}

	return 0;
}

#else //use libuv
#include "hp/hp_fs.h"

/////////////////////////////////////////////////////////////////////////////////////////

static void curl_perform(uv_poll_t *req, int status, int events)
{
	assert(req->data);
	hp_curleasy * citem = req->data;

    uv_timer_stop(&citem->hcurl->timer);
    int running_handles;
    int flags = 0;
    if (status < 0)                      flags = CURL_CSELECT_ERR;
    if (!status && (events & UV_READABLE)) flags |= CURL_CSELECT_IN;
    if (!status && (events & UV_WRITABLE)) flags |= CURL_CSELECT_OUT;

    curl_multi_socket_action(citem->hcurl->curl_handle, citem->fd, flags, &running_handles);
    check_multi_info(citem->hcurl);
}

static void curl_close_cb(uv_handle_t *handle)
{
	hp_curleasy *citem = (hp_curleasy*) handle->data;
	free(citem);
}

static hp_curleasy *create_curl_context(hp_curleasy * oldcitem, hp_curl * hcurl,
		curl_socket_t sockfd)
{
	assert(oldcitem && hcurl);

    hp_curleasy *citem;

    citem = (hp_curleasy*) malloc(sizeof *citem);
    *citem = *oldcitem;

    citem->fd = sockfd;

    int r = uv_poll_init_socket(hcurl->loop, &citem->poll_handle, sockfd);
    assert(r == 0);
    citem->poll_handle.data = citem;

    return citem;
}

static int handle_socket(CURL *easy, curl_socket_t s, int action, void *userp, void *socketp)
{
	hp_curl * hcurl = (hp_curl *) userp;
	assert(hcurl && hcurl->curl_handle);

	int rc;
	hp_curleasy * citem = 0;
	if (action == CURL_POLL_IN || action == CURL_POLL_OUT) {
		if(socketp){
			citem = (hp_curleasy * )socketp;
		}
		else{
			hp_curleasy * oldcitem = 0;
			curl_easy_getinfo(easy, CURLINFO_PRIVATE, &oldcitem);
			citem = create_curl_context(oldcitem, hcurl, s);

			curl_easy_setopt(easy, CURLOPT_WRITEDATA, citem);
			curl_easy_setopt(easy, CURLOPT_PRIVATE, citem);
			curl_multi_assign(hcurl->curl_handle, s, (void *) citem);
		}
    }

    switch (action) {
        case CURL_POLL_IN:
            uv_poll_start(&citem->poll_handle, UV_READABLE, curl_perform);
            break;
        case CURL_POLL_OUT:
            uv_poll_start(&citem->poll_handle, UV_WRITABLE, curl_perform);
            break;
        case CURL_POLL_REMOVE:
            if (socketp) {
            	citem = socketp;
                uv_poll_stop(&citem->poll_handle);
                uv_close((uv_handle_t*) &citem->poll_handle, curl_close_cb);
                curl_multi_assign(hcurl->curl_handle, s, NULL);
            }
            break;
        default:
            abort();
    }

    return 0;
}
#endif

#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
static int hp_curl_multi_handle_timeout(hp_timerfd * timerfd)
{
	assert(timerfd);
	hp_curl * hcurl = (hp_curl * )timerfd->arg;

	int running_handles;
	curl_multi_socket_action(hcurl->curl_handle, CURL_SOCKET_TIMEOUT, 0,
			&running_handles);

	check_multi_info(hcurl);
	return 0;
}
#else  //use libuv
static void on_timeout(uv_timer_t *req)
{
	assert(req->data);
	hp_curl *context = (hp_curl*) req->data;
    int running_handles;
    curl_multi_socket_action(context->curl_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);
    check_multi_info(context);
}
#endif

static int start_timeout(CURLM *multi, long timeout_ms, void *userp)
{
	assert(userp);
	hp_curl *hcurl = (hp_curl*) userp;
	if (timeout_ms <= 0){
		timeout_ms = 100; /* 0 means directly call socket_action, but we'll do it
		 in a bit */
#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
		hp_timerfd_reset(&hcurl->timer, timeout_ms);
#else  //use libuv
		uv_timer_start(&hcurl->timer, on_timeout, timeout_ms, 0);
#endif
	}
	return 0;
}

int hp_curladd(hp_curl * hcurl, CURL * curl, const char * url
		, struct curl_slist * hdrs
		, void * form
		, char const * resp
		, hp_curl_proress_cb_t on_proress
		, hp_curl_done_cb_t on_done
		, void * arg, int flags)
{
	if (!(hcurl && curl && url))
		return -1;

	FILE * f = 0;
	sds s = 0;
	if(resp){ /* save response? */
		if(strlen(resp) > 0){
			f = fopen(resp, "wb");
			if(!f)
				return -2;

			s = sdsnew(resp); /* filename */
		}
		else s = sdsempty(); /* the buffer */
	}


	hp_curleasy * citem = calloc(1, sizeof(hp_curleasy));
	citem->curl = curl;
	citem->on_proress = on_proress;
	citem->on_done = on_done;
	citem->arg = arg;
	citem->resp = s;
	citem->f = f;
	citem->hcurl = hcurl;
	citem->hdrs = hdrs;
	citem->url = sdsnew(url);

	if(flags == 0)
		citem->form = form;

    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl/7.81.0");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, multi_write_data);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, citem);
	curl_easy_setopt(curl, CURLOPT_PRIVATE, citem);
//	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // Enable automatic redirection

	/* headers */
	if(hdrs)
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
	/* body */
	if(form){
		if(flags){
			char const * body =  form;
//			curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, strlen(body));
//			curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body);
			curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, body);
		}
		else{
			curl_easy_setopt(curl, CURLOPT_POST, 1);
#if (LIBCURL_VERSION_MAJOR >=7 && LIBCURL_VERSION_MINOR >= 56)
			curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
#else
			curl_easy_setopt(curl, CURLOPT_HTTPPOST, form);
#endif /* LIBCURL_VERSION_MINOR */
		}
	}
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_multi_add_handle(hcurl->curl_handle, curl);
	return 0;
}

int hp_curlinit(hp_curl * hcurl, hp_curl_loop_t * loop)
{
	int rc;
	if(!(hcurl && loop))
		return -1;

	memset(hcurl, 0, sizeof(hp_curl));
	hcurl->loop = loop;
	hcurl->curl_handle = curl_multi_init();

	curl_multi_setopt(hcurl->curl_handle, CURLMOPT_SOCKETFUNCTION, handle_socket);
	curl_multi_setopt(hcurl->curl_handle, CURLMOPT_SOCKETDATA, hcurl);
	curl_multi_setopt(hcurl->curl_handle, CURLMOPT_TIMERFUNCTION, start_timeout);
	curl_multi_setopt(hcurl->curl_handle, CURLMOPT_TIMERDATA, hcurl);
#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
	if(hp_timerfd_init(&hcurl->timer, loop, hp_curl_multi_handle_timeout, 0, hcurl) != 0) { return -1; }
#else  //use libuv
	if(!(uv_timer_init(loop, &hcurl->timer) == 0)) { return -1 };
	hcurl->timer.data = hcurl;
#endif

	return 0;
}

void hp_curluninit(hp_curl * hcurl)
{
	if(!hcurl)
		return;
#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
	hp_timerfd_uninit(&hcurl->timer);
#else  //use libuv
#endif
	curl_multi_cleanup(hcurl->curl_handle);
}

/////////////////////////////////////////////////////////////////////////////////////////
/* tests */
#ifndef NDEBUG

#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>	/*fstat*/
#include <curl/curl.h>   /* libcurl */
#include "hp/hp_curl.h"
#include "hp/hp_assert.h"
#include "hp/hp_ssl.h"
#include "hp/string_util.h"
#include "hp/str_dump.h"

#define TEST_URL "https://mirrors.aliyun.com/cygwin/x86_64/release/vim/vim-8.2.4372-2.tar.xz"
#define TEST_SHA256 "d6e079e9867d0805dd3e5b7fe754670d72d04dee0c1e191fbeb8ee05553d63e8"
#define TEST_FSIZE 1479988
#define TEST_FILE "vim-8.2.4372-2.tar.xz"

typedef struct hp_curltest_ctx {
	int test;
	int n;
	int done;
} hp_curltest_ctx;

static int on_progress(int bytes, int content_length, sds resp, void * arg)
{
	static time_t lastt = 0;
	if(lastt == 0 || time(0) - lastt > 2){
		hp_log(stdout, "%s: download %d/%d, %.1f%%\n", __FUNCTION__
			, bytes, content_length, content_length > 0 ? bytes * 100.0 / content_length : 0);

		lastt = time(0);
	}
	return 0;
}

static int on_file(hp_curl * hcurl, CURL *easy_handle, char const * url, sds str, void * arg)
{
	hp_curltest_ctx * c = (hp_curltest_ctx * )arg;
	hp_assert_path(TEST_FILE, REG);
	sds buf = hp_fread(TEST_FILE);
	hp_assert(sdslen(buf) == TEST_FSIZE, "%i!=%i", sdslen(buf), TEST_FSIZE);
#ifdef LIBHP_WITH_SSL
	sds hash = hp_ssl_sha256(buf, sdslen(buf));
	hp_assert(strncasecmp(hash, TEST_SHA256, strlen(TEST_SHA256)) == 0,
		"len=%i, hash='%s', TEST_SHA256='%s'", sdslen(buf), hash, TEST_SHA256);
	sdsfree(hash);
#endif //LIBHP_WITH_SSL
	sdsfree(buf);

	if(c/* && c->test == 0*/) {
		--c->n;
		if(c->n == 0)
			c->done = 1;
	}

	return 0;
}

static int on_buffer(hp_curl * hcurl, CURL *easy_handle, char const * url, sds str, void * arg)
{
	assert(str);
	hp_curltest_ctx * c = (hp_curltest_ctx * )arg;
	hp_assert(sdslen(str) == TEST_FSIZE, "%i!=%i", sdslen(str), TEST_FSIZE);
#ifdef LIBHP_WITH_SSL
	sds hash = hp_ssl_sha256(str, sdslen(str));
	hp_assert(strncasecmp(hash, TEST_SHA256, strlen(TEST_SHA256)) == 0,
		"len=%i, hash='%s', TEST_SHA256='%s'", sdslen(str), hash, TEST_SHA256);
	sdsfree(hash);
#endif //LIBHP_WITH_SSL

	if(c/* && c->test == 0*/) {
		--c->n;
		if(c->n == 0)
			c->done = 1;
	}

	return 0;
}

static int on_upload(hp_curl * hcurl, CURL *easy_handle, char const * url, sds str, void * arg)
{
	hp_log(stdout, "%s: response='%s'\n", __FUNCTION__, str);
	return 0;
}

int test_hp_curl_main(int argc, char ** argv)
{
	int rc;
	if(curl_global_init(CURL_GLOBAL_ALL))
		return -1;
//	hp_assert_path("test_hp_curl_main/", DIR);
	{
		struct curl_slist * hdrs = 0;
		char * defhdrstr = strdup("Content-Type=application/json");
		hp_curl_append_header_from_qeury_str(&hdrs, defhdrstr); assert(hdrs); assert(hdrs);
		free(defhdrstr);
	}
	/* easy mode */
	{
		sds str = hp_curl_easy_perform(TEST_URL, 0/*curl_slist*/, 0, 0);
		assert(str);
		hp_log(stdout, "%s: from url='%s', response='%s'\n", __FUNCTION__, TEST_URL
				, dumpstr(str, sdslen(str), 128));
#ifdef LIBHP_WITH_SSL
		sds hash = hp_ssl_sha256(str, sdslen(str));
		hp_assert(strncasecmp(hash, TEST_SHA256, strlen(TEST_SHA256)) == 0,
			"len=%i, hash='%s', TEST_SHA256='%s'", sdslen(str), hash, TEST_SHA256);
		sdsfree(hash);
#else
		hp_assert(sdslen(str) == TEST_FSIZE, "%i!=%i", sdslen(str), TEST_FSIZE);
#endif //LIBHP_WITH_SSL
		sdsfree(str);
	}
	/* with huge size json file */
	////////////////////////////////////////////////
	/* download to buffer */
	{
		hp_curltest_ctx cobj  = { 0, 1 }, * c = &cobj;
#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
		hp_epoll efdsobj, * loop = &efdsobj; rc = hp_epoll_init(loop, 200, 200, 0, 0); assert(rc == 0);
#else
		uv_loop_t uvloop, * loop = &uvloop; uv_loop_init(loop);
#endif

		hp_curl hp_curl_multiobj, * hcurl = &hp_curl_multiobj;
		rc = hp_curlinit(hcurl, loop);
		assert(rc == 0);

		rc = hp_curladd(hcurl, curl_easy_init(), TEST_URL, 0, 0, "", on_progress, on_buffer, c, 0);
		assert(rc == 0);

		for(;!c->done;)
#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
			hp_epoll_run(loop, 1);
#else
			uv_run(loop, UV_RUN_ONCE);
#endif

		hp_curluninit(hcurl);
#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
		hp_epoll_uninit(loop);
#else
		uv_loop_close(loop);
#endif
	}
	/* download to file */
	{
		hp_curltest_ctx cobj  = { 0, 1 }, * c = &cobj;
#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
		hp_epoll efdsobj, * loop = &efdsobj; rc = hp_epoll_init(loop, 200, 200, 0, 0); assert(rc == 0);
#else
		uv_loop_t uvloop, * loop = uv_default_loop();
#endif
		hp_curl hp_curl_multiobj, *hcurl = &hp_curl_multiobj;
		rc = hp_curlinit(hcurl, loop);
		assert(rc == 0);

		rc = hp_curladd(hcurl, curl_easy_init(), TEST_URL
			, 0, 0, TEST_FILE, on_progress, on_file, c, 0);
		assert(rc == 0);

		for(;!c->done;)
#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
			hp_epoll_run(loop, 1);
#else
			uv_run(loop, UV_RUN_ONCE);
#endif

		hp_curluninit(hcurl);
#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
		hp_epoll_uninit(loop);
#else
		uv_loop_close(loop);
#endif
	}
	/* multi mode */
	{
		int rc;
		hp_curltest_ctx cobj  = { 0, 3 }, * c = &cobj;
#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
		hp_epoll efdsobj, * loop = &efdsobj;
		rc = hp_epoll_init(loop, 200, 200, 0, 0); assert(rc == 0);
#else
		uv_loop_t uvloop, * loop = &uvloop; uv_loop_init(loop);
#endif

		hp_curl hp_curl_multiobj, * hcurl = &hp_curl_multiobj;
		rc = hp_curlinit(hcurl, loop);
		assert(rc == 0);

		rc = hp_curladd(hcurl, curl_easy_init(), TEST_URL, 0, 0, 0, on_progress, 0/*on_buffer*/, c, 0);
		assert(rc == 0);

		rc = hp_curladd(hcurl, curl_easy_init(), TEST_URL, 0, 0, "", on_progress, on_buffer, c, 0);
		assert(rc == 0);

		rc = hp_curladd(hcurl, curl_easy_init(), TEST_URL, 0, 0, "", 0/*on_progress*/, on_buffer, c, 0);
		assert(rc == 0);

		rc = hp_curladd(hcurl, curl_easy_init(), TEST_URL, 0, 0, TEST_FILE, on_progress, on_file, c, 0);
		assert(rc == 0);

		for(;!c->done;)
#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
			hp_epoll_run(loop, 1);
#else
			uv_run(loop, UV_RUN_ONCE);
#endif
		hp_curluninit(hcurl);

#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
		hp_epoll_uninit(loop);
#else
		uv_loop_close(loop);
#endif
	}
	/* upload file */
	goto ret_;
	{
#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
		hp_epoll efdsobj, * loop = &efdsobj; rc = hp_epoll_init(loop, 200, 200, 0, 0); assert(rc == 0);
#else
		uv_loop_t uvloop, * loop = uv_default_loop();
#endif

		hp_curl hp_curl_multiobj, *hcurl = &hp_curl_multiobj;
		rc = hp_curlinit(hcurl, loop);
		assert(rc == 0);

		CURL * curl = curl_easy_init();

		char defhdrstr[] = "";
		struct curl_slist * curl_hdrs = 0;
		hp_curl_append_header_from_qeury_str(&curl_hdrs
				, defhdrstr);

#if (LIBCURL_VERSION_MAJOR >=7 && LIBCURL_VERSION_MINOR >= 56)
		curl_mime *form = NULL;
		curl_mimepart *field = NULL;

		/* Create the form */
		form = curl_mime_init(curl);
	    /* Fill in the file upload field */
	    field = curl_mime_addpart(form);
	    curl_mime_name(field, "fileList");
	    curl_mime_filedata(field, TEST_FILE);

	    /* Fill in the filename field */
	    field = curl_mime_addpart(form);
	    curl_mime_name(field, "filename");
	    curl_mime_data(field, TEST_FILE, CURL_ZERO_TERMINATED);
#else
		struct curl_httppost *form=NULL;
		struct curl_httppost *lastptr=NULL;
		/* Fill in the file upload field. This makes libcurl load data from
		 the given file name when curl_easy_perform() is called. */
		curl_formadd(&form,
				   &lastptr,
				   CURLFORM_COPYNAME, "fileList",
				   CURLFORM_FILE, TEST_FILE,
				   CURLFORM_END);
#endif /* LIBCURL_VERSION_MINOR */

		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
		rc = hp_curladd(hcurl, curl, "" , 0, form, "", 0, on_upload, hcurl, 0);
		assert(rc == 0);

#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
		hp_epoll_run(loop, 0);
#else
		uv_run(loop, UV_RUN_DEFAULT);
#endif

		hp_curluninit(hcurl);
#if (defined HAVE_SYS_TIMERFD_H) && (defined HAVE_SYS_EPOLL_H)
		hp_epoll_uninit(loop);
#else
		uv_loop_close(loop);
#endif
	}
ret_:
	curl_global_cleanup();

	return rc;
}

#endif /* NDEBUG */

#endif /* LIBHP_WITH_CURL */
