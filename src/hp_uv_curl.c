/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2019/8/8
 *
 * curl using libuv
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_CURL

#include "hp_uv_curl.h"  /* hp_uv_curlm */
#include "hp_log.h"
#include "hp_fs.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>       /* errno */
#include <curl/curl.h>   /* libcurl */
#include "sds/sds.h"     /* sds */
#include "c-vector/cvector.h"

/////////////////////////////////////////////////////////////////////////////////////////

typedef struct hp_uv_curlm_easy {
	CURL * 		curl;
	uv_poll_t 	poll_handle;
	curl_socket_t fd;

	sds      	resp;   /* the response */
	FILE *      f;      /* the response, write to file? */

	/* for progress */
	int 	    bytes;
	int 	    content_length;

	hp_uv_curl_proress_cb_t on_proress;
	hp_uv_curl_done_cb_t on_done;
	void *      arg;

	hp_uv_curlm * curlm;  /* ref to context */

	struct curl_slist * hdrs;
#if (LIBCURL_VERSION_MAJOR >=7 && LIBCURL_VERSION_MINOR >= 56)
	curl_mime *	form;
#else
	struct curl_httppost * form;
#endif /* LIBCURL_VERSION_MINOR */
} hp_uv_curlm_easy;

static size_t multi_write_data(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	hp_uv_curlm_easy * citem = (hp_uv_curlm_easy *)userdata;

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

/////////////////////////////////////////////////////////////////////////////////////////

static void destroy_curl_context(hp_uv_curlm_easy * citem)
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
}

static void check_multi_info(hp_uv_curlm * curlm) {
    char *done_url;
    CURLMsg *message;
    int pending;
    hp_uv_curlm_easy * citem = 0;

    while ((message = curl_multi_info_read(curlm->curl_handle, &pending))) {
        switch (message->msg) {
        case CURLMSG_DONE:
            curl_easy_getinfo(message->easy_handle, CURLINFO_EFFECTIVE_URL,
                            &done_url);
			curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &citem);
			assert(citem);

			if(citem->f) fclose(citem->f);
			if(citem->on_done)
				citem->on_done(curlm, done_url, citem->resp, citem->arg);

			destroy_curl_context(citem);

            curl_multi_remove_handle(curlm->curl_handle, message->easy_handle);

            curl_easy_cleanup(message->easy_handle);
     		break;

        default:
            fprintf(stderr, "CURLMSG default\n");
            assert(0);
            abort();
        }
    }
}

static void curl_perform(uv_poll_t *req, int status, int events)
{
	assert(req->data);
	hp_uv_curlm_easy * citem = req->data;

    uv_timer_stop(&citem->curlm->timeout);
    int running_handles;
    int flags = 0;
    if (status < 0)                      flags = CURL_CSELECT_ERR;
    if (!status && (events & UV_READABLE)) flags |= CURL_CSELECT_IN;
    if (!status && (events & UV_WRITABLE)) flags |= CURL_CSELECT_OUT;

    curl_multi_socket_action(citem->curlm->curl_handle, citem->fd, flags, &running_handles);
    check_multi_info(citem->curlm);
}

static void on_timeout(uv_timer_t *req)
{
	assert(req->data);
	hp_uv_curlm *context = (hp_uv_curlm*) req->data;
    int running_handles;
    curl_multi_socket_action(context->curl_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);
    check_multi_info(context);
}

static int start_timeout(CURLM *multi, long timeout_ms, void *userp)
{
	assert(userp);
	hp_uv_curlm *context = (hp_uv_curlm*) userp;
	if (timeout_ms <= 0){
		timeout_ms = 1; /* 0 means directly call socket_action, but we'll do it
		 in a bit */
		uv_timer_start(&context->timeout, on_timeout, timeout_ms, 0);
	}
	return 0;
}

static void curl_close_cb(uv_handle_t *handle)
{
	hp_uv_curlm_easy *citem = (hp_uv_curlm_easy*) handle->data;
	free(citem);
}

static hp_uv_curlm_easy *create_curl_context(hp_uv_curlm_easy * oldcitem, hp_uv_curlm * curlm,
		curl_socket_t sockfd)
{
	assert(oldcitem && curlm);

    hp_uv_curlm_easy *citem;

    citem = (hp_uv_curlm_easy*) malloc(sizeof *citem);
    *citem = *oldcitem;

    citem->fd = sockfd;

    int r = uv_poll_init_socket(curlm->loop, &citem->poll_handle, sockfd);
    assert(r == 0);
    citem->poll_handle.data = citem;

    return citem;
}

static int handle_socket(CURL *easy, curl_socket_t s, int action, void *userp, void *socketp)
{
	hp_uv_curlm * curlm = (hp_uv_curlm *) userp;
	assert(curlm && curlm->curl_handle);

	int rc;
	hp_uv_curlm_easy * citem = 0;
	if (action == CURL_POLL_IN || action == CURL_POLL_OUT) {
		if(socketp){
			citem = (hp_uv_curlm_easy * )socketp;
		}
		else{
			hp_uv_curlm_easy * oldcitem = 0;
			curl_easy_getinfo(easy, CURLINFO_PRIVATE, &oldcitem);
			citem = create_curl_context(oldcitem, curlm, s);

			curl_easy_setopt(easy, CURLOPT_WRITEDATA, citem);
			curl_easy_setopt(easy, CURLOPT_PRIVATE, citem);
			curl_multi_assign(curlm->curl_handle, s, (void *) citem);
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
                curl_multi_assign(curlm->curl_handle, s, NULL);
            }
            break;
        default:
            abort();
    }

    return 0;
}

int hp_uv_curlm_add(hp_uv_curlm * curlm, CURL * handle, const char * url
		, struct curl_slist * hdrs
		, void * form
		, char const * resp
		, hp_uv_curl_proress_cb_t on_proress
		, hp_uv_curl_done_cb_t on_done
		, void * arg, int flags)
{
	if (!(curlm && handle && url))
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


	hp_uv_curlm_easy * citem = calloc(1, sizeof(hp_uv_curlm_easy));
	citem->curl = handle;
	citem->on_proress = on_proress;
	citem->on_done = on_done;
	citem->arg = arg;
	citem->resp = s;
	citem->f = f;
	citem->curlm = curlm;
	citem->hdrs = hdrs;

	if(flags == 0)
		citem->form = form;

	curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, multi_write_data);
	curl_easy_setopt(handle, CURLOPT_WRITEDATA, citem);
	curl_easy_setopt(handle, CURLOPT_PRIVATE, citem);

	/* headers */
	if(hdrs)
		curl_easy_setopt(handle, CURLOPT_HTTPHEADER, hdrs);
	/* body */
	if(form){
		if(flags){
			char const * body =  form;
//			curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, strlen(body));
//			curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body);
			curl_easy_setopt(handle, CURLOPT_COPYPOSTFIELDS, body);
		}
		else{
			curl_easy_setopt(handle, CURLOPT_POST, 1);
#if (LIBCURL_VERSION_MAJOR >=7 && LIBCURL_VERSION_MINOR >= 56)
			curl_easy_setopt(handle, CURLOPT_MIMEPOST, form);
#else
			curl_easy_setopt(handle, CURLOPT_HTTPPOST, form);
#endif /* LIBCURL_VERSION_MINOR */
		}
	}


	curl_easy_setopt(handle, CURLOPT_URL, url);
	curl_multi_add_handle(curlm->curl_handle, handle);

	return 0;
}

int hp_uv_curlm_init(hp_uv_curlm * curlm, uv_loop_t * loop)
{
	int rc;
	if(!(curlm && loop))
		return -1;

	if(curl_global_init(CURL_GLOBAL_ALL))
		return -1;

	memset(curlm, 0, sizeof(hp_uv_curlm));
	curlm->loop = loop;

	curlm->curl_handle = curl_multi_init();
	curl_multi_setopt(curlm->curl_handle, CURLMOPT_SOCKETFUNCTION, handle_socket);
	curl_multi_setopt(curlm->curl_handle, CURLMOPT_SOCKETDATA, curlm);
	curl_multi_setopt(curlm->curl_handle, CURLMOPT_TIMERFUNCTION, start_timeout);
	curl_multi_setopt(curlm->curl_handle, CURLMOPT_TIMERDATA, curlm);

	rc = uv_timer_init(loop, &curlm->timeout);
	assert(rc == 0);
	curlm->timeout.data = curlm;

	return 0;
}

void hp_uv_curlm_uninit(hp_uv_curlm * curlm)
{
	if(!curlm)
		return;

	curl_multi_cleanup(curlm->curl_handle);
}

/////////////////////////////////////////////////////////////////////////////////////////
/* tests */
#ifndef NDEBUG

#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>	/*fstat*/
#include <curl/curl.h>   /* libcurl */
#include "hp_curl.h"
#include "hp_assert.h"
#include "hp_ssl.h"
#include "string_util.h"

#define TEST_URL "https://mirrors.aliyun.com/cygwin/x86_64/release/vim/vim-8.2.4372-2.tar.xz"
#define TEST_SHA256 "d6e079e9867d0805dd3e5b7fe754670d72d04dee0c1e191fbeb8ee05553d63e8"
#define TEST_FSIZE 1479988
#define TEST_FILE "test_hp_uv_curl_main/vim-8.2.4372-2.tar.xz"

static int on_progress(int bytes, int content_length, sds resp, void * arg)
{
	static time_t lastt = 0;
	if(time(0) - lastt > 2){
		fprintf(stdout, "%s: download %d/%d, %.1f%%\n", __FUNCTION__
			, bytes, content_length, content_length > 0 ? bytes * 100.0 / content_length : 0);

		lastt = time(0);
	}
	return 0;
}

static int on_file(hp_uv_curlm * curlm, char const * url, sds str_, void * arg)
{
	hp_assert_path(TEST_FILE, REG);
	sds str = hp_fread(TEST_FILE);
	hp_assert(sdslen(str) == TEST_FSIZE, "%i!=%i", sdslen(str), TEST_FSIZE);
#ifdef LIBHP_WITH_SSL
	sds hash = hp_ssl_sha256(str, sdslen(str));
	hp_assert(strncasecmp(hash, TEST_SHA256, strlen(TEST_SHA256)) == 0,
		"len=%i, hash='%s', TEST_SHA256='%s'", sdslen(str), hash, TEST_SHA256);
	sdsfree(hash);
#endif //LIBHP_WITH_SSL
	sdsfree(str);

	return 0;
}

static int on_buffer(hp_uv_curlm * curlm, char const * url, sds str, void * arg)
{
	assert(str);
	hp_assert(sdslen(str) == TEST_FSIZE, "%i!=%i", sdslen(str), TEST_FSIZE);
#ifdef LIBHP_WITH_SSL
	sds hash = hp_ssl_sha256(str, sdslen(str));
	hp_assert(strncasecmp(hash, TEST_SHA256, strlen(TEST_SHA256)) == 0,
		"len=%i, hash='%s', TEST_SHA256='%s'", sdslen(str), hash, TEST_SHA256);
	sdsfree(hash);
#endif //LIBHP_WITH_SSL

	return 0;
}

static int on_upload(hp_uv_curlm * curlm, char const * url, sds str, void * arg)
{
	fprintf(stdout, "%s: response='%s'\n", __FUNCTION__, str);
	return 0;
}

int test_hp_uv_curl_main(int argc, char ** argv)
{
	int rc;
	hp_assert_path("test_hp_uv_curl_main/", DIR);

	/* download to buffer */
	{
		uv_loop_t uvloop, * loop = &uvloop; uv_loop_init(loop);

		hp_uv_curlm hp_curl_multiobj, * curlm = &hp_curl_multiobj;
		rc = hp_uv_curlm_init(curlm, loop);
		assert(rc == 0);

		rc = hp_uv_curlm_add(curlm, curl_easy_init(), TEST_URL
				, 0, 0, "", on_progress, on_buffer, curlm, 0);
		assert(rc == 0);
		hp_log(stdout, "%s: downloading %s ...\n", __FUNCTION__, TEST_URL);

		uv_run(loop, UV_RUN_DEFAULT);

		hp_uv_curlm_uninit(curlm);
		uv_loop_close(loop);
	}
	/* download to file */
	{
		uv_loop_t * loop = uv_default_loop();

		hp_uv_curlm hp_curl_multiobj, *curlm = &hp_curl_multiobj;
		rc = hp_uv_curlm_init(curlm, loop);
		assert(rc == 0);

		rc = hp_uv_curlm_add(curlm, curl_easy_init(), TEST_URL
			, 0, 0, TEST_FILE, on_progress, on_file, curlm, 0);
		assert(rc == 0);
		hp_log(stdout, "%s: downloading %s ...\n", __FUNCTION__, TEST_URL);

		uv_run(loop, UV_RUN_DEFAULT);

		hp_uv_curlm_uninit(curlm);
		uv_loop_close(loop);
	}

	/* ignore response */
	{
		uv_loop_t uvloop, * loop = &uvloop; uv_loop_init(loop);

		hp_uv_curlm hp_curl_multiobj, *curlm = &hp_curl_multiobj;
		rc = hp_uv_curlm_init(curlm, loop);
		assert(rc == 0);
		hp_log(stdout, "%s: downloading %s ...\n", __FUNCTION__, TEST_URL);

		rc = hp_uv_curlm_add(curlm, curl_easy_init(), TEST_URL
			, 0, 0, 0, on_progress, 0, curlm, 0);
		assert(rc == 0);

		uv_run(loop, UV_RUN_DEFAULT);

		hp_uv_curlm_uninit(curlm);
		uv_loop_close(loop);
	}

	/* upload file */
	{
		uv_loop_t uvloop, * loop = &uvloop; uv_loop_init(loop);

		hp_uv_curlm hp_curl_multiobj, *curlm = &hp_curl_multiobj;
		rc = hp_uv_curlm_init(curlm, loop);
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
		rc = hp_uv_curlm_add(curlm, curl, ""
			, 0, form, "", 0, on_upload, curlm, 0);
		assert(rc == 0);

		uv_run(loop, UV_RUN_DEFAULT);

		hp_uv_curlm_uninit(curlm);
		uv_loop_close(loop);
	}

	curl_global_cleanup();

	return rc;
}

#endif /* NDEBUG */
#endif
