#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <getopt.h>
#include <errno.h>

#include <curl/curl.h>
#include <cjson/cJSON.h>





/////////////////////////////////////////////////////////////////////////////
// strings

typedef struct string {
	size_t size;
	size_t max;
	char * data;
} string_t;

void string_init(string_t * str, size_t max) {
	str->size = 0;
	str->max = max;
	str->data = malloc(max);
}

void string_free(string_t * str) {
	if (str->data)
		free(str->data);
}

void string_append_char(string_t * str, char ch) {
	if (str->size == str->max) {
		str->max *= 2;
		str->data = realloc(str->data, str->max);
	}
	str->data[str->size++] = ch;
}

void string_append_chars(string_t * str, const char * new, size_t new_len) {
	size_t total_size = str->size + new_len;
	
	int needs_realloc = 0;
	while (total_size > str->max) {
		needs_realloc++;
		str->max *= 2;
	}
	if (needs_realloc) {
		str->data = realloc(str->data, str->max);
	}
	
	memcpy(str->data + str->size, new, new_len);
	str->size += new_len;
}

void string_append_string(string_t * str, const char * new) {
	size_t new_len = strlen(new);
	string_append_chars(str, new, new_len);
}

// this should be used as an argument to CURLOPT_WRITEFUNCTION
// the CURLOPT_WRITEDATA should be a string pointer
size_t string_append_curl_callback(char * ptr, size_t size, size_t nmemb, void * userdata) {
	string_t * str = (string_t *)userdata;
	size_t bytes = size * nmemb;
	string_append_chars(str, ptr, bytes);
	return bytes;
}

#define string_append_const_string(str, charp) string_append_chars(str, charp, sizeof(charp)-1)

#define string_append_0(str) string_append_char(str, '\0');



/////////////////////////////////////////////////////////////////////////////
// global stuff

#define BASE "https://www.pixiv.net/ajax"
#define VERSION "7df844931e62b878f9ebfc3acc79d77ce7d10e6b"
#define LANG "en"

#define USER_AGENT_PRE "User-Agent: "
#define USER_AGENT "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:124.0) Gecko/20100101 Firefox/124.0"

#define CONTENT_TYPE_NORMAL "content-type: application/x-www-form-urlencoded; charset=utf-8"
#define CONTENT_TYPE_JSON "content-type: application/json; charset=utf-8"

#define COOKIES_PRE "Cookie: "
string_t cookies_str = {0,0,0};

#define CSRF_TOKEN_PRE "x-csrf-token: "
char * csrf_token = NULL;

#undef min
#undef max
#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))





////////////////////////////////////////////////////////////////////////////
// cookies

#define DOMAIN "pixiv.net"
#define DOMAIN_SIZE 9

int read_cookies_from_file(const char * filename) {
	const char * delim = "\t";
	
	FILE * f = fopen(filename, "r");
	if (!f) {
		int en = errno;
		printf("%s: %s\n", filename, strerror(en));
		return en;
	}
	
	string_init(&cookies_str, 512);
	string_append_const_string(&cookies_str, COOKIES_PRE);
	char line[512];
	int amt_cookies = 0;
	int lineno = 0;
	time_t cur_time = time(NULL);
	while (1) {
		lineno++;
		if (!fgets(line, 512, f)) {
			if (ferror(f)) {
				int en = errno;
				fclose(f);
				printf("%s read: %s\n", filename, strerror(en));
				return en;
			}
			fclose(f);
			string_append_char(&cookies_str, '\0');
			puts(cookies_str.data);
			return 0;
		}
		
		size_t line_len = strlen(line);
		if (line[0] == '#')
			continue;
		if (line[line_len-1] == '\n')
			line[--line_len] = '\0';
		
		char * domain = strtok(line, delim);
		if (!domain)
			continue;
		size_t domain_len = strlen(domain);
		if (domain_len < DOMAIN_SIZE)
			continue;
		if (memcmp(domain + domain_len - DOMAIN_SIZE, DOMAIN, DOMAIN_SIZE))
			continue;
		
		if (!strtok(NULL, delim)) // don't care about subdomains
			continue;
		if (!strtok(NULL, delim)) // don't care about path
			continue;
		if (!strtok(NULL, delim)) // don't care about https
			continue;
		
		char * expiry_str = strtok(NULL, delim);
		if (!expiry_str)
			continue;
		long expiry = strtol(expiry_str, NULL, 0);
		
		char * name = strtok(NULL, delim);
		if (!name)
			continue;
		char * value = strtok(NULL, delim);
		if (!value)
			continue;
		size_t name_len = strlen(name);
		size_t value_len = strlen(value);
		
		if (cur_time >= expiry) {
			printf("WARNING: ignoring %s=%s because it has expired\n", name, value);
			continue;
		}
		
		if (amt_cookies) {
			string_append_char(&cookies_str, ';');
			string_append_char(&cookies_str, ' ');
		}
		string_append_chars(&cookies_str, name, name_len);
		string_append_char(&cookies_str, '=');
		string_append_chars(&cookies_str, value, value_len);
		amt_cookies++;
	}
}






//////////////////////////////////////////////////////////////////////
// generic

// unused parameters "ptr" and "userdata" are required by interface
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
size_t curl_null_write_callback(char * ptr, size_t size, size_t nmemb, void * userdata) {
	return size * nmemb;
}
#pragma GCC diagnostic pop

CURL * make_curl() {
	CURL * curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
	if (cookies_str.data)
		curl_easy_setopt(curl, CURLOPT_COOKIE, cookies_str.data + sizeof(COOKIES_PRE)-1);
	curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
	return curl;
}

CURL * make_curl_post(const char * content_type, struct curl_slist ** slist_pp) {
	CURL * curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_POST, 1);
	
	struct curl_slist * slist = NULL;
	slist = curl_slist_append(slist, "Accept: application/json");
	slist = curl_slist_append(slist, content_type);
	slist = curl_slist_append(slist, USER_AGENT_PRE USER_AGENT);
	if (cookies_str.data)
		slist = curl_slist_append(slist, cookies_str.data);
	slist = curl_slist_append(slist, csrf_token);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
	*slist_pp = slist;
	
	return curl;
}

#define RETRY_ERROR "An error occurred. Try again in a while."
#define CAN_RETRY ((cJSON *)-1)
unsigned retry_delay_tbl[] = {5,15,30};
#define MAX_RETRIES (sizeof(retry_delay_tbl) / sizeof(*retry_delay_tbl))

int fetch_ignore_response(const char * url, const char * referer, CURL * curl) {
	if (url)
		curl_easy_setopt(curl, CURLOPT_URL, url);
	if (referer)
		curl_easy_setopt(curl, CURLOPT_REFERER, referer);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_null_write_callback);
	CURLcode res = curl_easy_perform(curl);
	if (res)
		puts(curl_easy_strerror(res));
	return res;
}

int fetch_to_string(const char * url, const char * referer, string_t * str, CURL * curl) {
	if (url)
		curl_easy_setopt(curl, CURLOPT_URL, url);
	if (referer)
		curl_easy_setopt(curl, CURLOPT_REFERER, referer);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, string_append_curl_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, str);
	CURLcode res = curl_easy_perform(curl);
	if (res)
		puts(curl_easy_strerror(res));
	return res;
}

cJSON * fetch_json(const char * url, const char * referer, string_t * json, CURL * curl) {
	if (fetch_to_string(url, referer, json, curl))
		return NULL;
	cJSON * cjson = cJSON_ParseWithLength(json->data, json->size);
	if (!cjson) {
		puts("json parse error");
	}
	return cjson;
}

cJSON * get_json_body(cJSON * cjson) {
	if (!cjson) {
		puts("json parse error");
		return NULL;
	}
	cJSON * error = cJSON_GetObjectItem(cjson, "error");
	if (!error) {
		puts("can't determine error state");
		return NULL;
	}
	if (cJSON_IsTrue(error)) {
		cJSON * message = cJSON_GetObjectItem(cjson, "message");
		char * message_str = cJSON_GetStringValue(message);
		if (message_str) {
			puts(message_str);
			if (!strcmp(message_str, RETRY_ERROR))
				return CAN_RETRY;
		}
		else
			puts("can't get error string");
		return NULL;
	}
	
	cJSON * body = cJSON_GetObjectItem(cjson, "body");
	if (!body) {
		puts("can't get response body");
	}
	return body;
}

cJSON * fetch_json_body(const char * url, const char * referer, string_t * json, cJSON ** cjson_ptr, CURL * curl) {
	int retries = 0;
	while (1) {
		// fetch
		json->size = 0;
		cJSON * cjson = fetch_json(url, referer, json, curl);
		*cjson_ptr = cjson;
		if (!cjson)
			return NULL;
		
		// try getting body
		cJSON * body = get_json_body(cjson);
		if (body != CAN_RETRY)
			return body;
		
		// retry
		cJSON_Delete(cjson);
		*cjson_ptr = NULL;
		if (retries == MAX_RETRIES)
			return NULL;
		unsigned delay = retry_delay_tbl[retries++];
		printf("...Retrying in %u seconds...", delay);
		while (delay)
			delay = sleep(delay);
	}
}

int save_file_from_url(char * path_buf, char * filename_buf, const char * url, CURL * curl) {
	const char * original_filename = strrchr(url, '/');
	if (!original_filename)
		original_filename = url;
	else
		original_filename++;
	printf("%s...", original_filename);
	
	strcpy(filename_buf, original_filename);
	FILE * f = fopen(path_buf, "wb");
	if (!f) {
		puts(strerror(errno));
		return -1;
	}
	
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
	CURLcode res = curl_easy_perform(curl);
	fclose(f);
	if (res) {
		puts(curl_easy_strerror(res));
		return -1;
	} else {
		puts("OK");
		return 0;
	}
}













////////////////////////////////////////////////////////////////////
// csrf token

#define HOME "https://www.pixiv.net/" LANG "/"

#define GLOBAL_DATA_PRE "<meta name=\"global-data\" id=\"meta-global-data\" content='"

int fetch_csrf_token() {
	printf("Getting CSRF token...");
	
	int status = -1;
	
	CURL * curl = make_curl();
	cJSON * cjson = NULL;
	string_t html;
	string_init(&html, 0x4000);
	
	status = fetch_to_string(HOME, NULL, &html, curl);
	if (status)
		goto cleanup;
	
	status = -1;
	for (size_t i = 0; i < html.size - sizeof(GLOBAL_DATA_PRE); i++) {
		if (!memcmp(html.data+i, GLOBAL_DATA_PRE, sizeof(GLOBAL_DATA_PRE)-1)) {
			size_t json_end_index = i + sizeof(GLOBAL_DATA_PRE)-1;
			for ( ; json_end_index < html.size; json_end_index++) {
				if (html.data[json_end_index] == '\'') {
					break;
				}
			}
			if (json_end_index == html.size) {
				puts("can't find end of global data json");
				goto done_search;
			}
			
			i += sizeof(GLOBAL_DATA_PRE)-1;
			cjson = cJSON_ParseWithLength(html.data + i, json_end_index - 1);
			if (!cjson) {
				puts("can't parse global data json");
				goto done_search;
			}
			cJSON * token = cJSON_GetObjectItem(cjson, "token");
			if (!token) {
				puts("can't find token");
				goto done_search;
			}
			char * token_str = cJSON_GetStringValue(token);
			size_t token_len = strlen(token_str);
			csrf_token = malloc(sizeof(CSRF_TOKEN_PRE) + token_len);
			memcpy(csrf_token + 0, CSRF_TOKEN_PRE, sizeof(CSRF_TOKEN_PRE)-1);
			memcpy(csrf_token + sizeof(CSRF_TOKEN_PRE)-1, token_str, token_len+1);
			goto done_search;
		}
	}
	puts("can't find global data in page");
done_search:
	
	if (csrf_token) {
		puts(csrf_token);
		status = 0;
	}
	
cleanup:
	string_free(&html);
	cJSON_Delete(cjson);
	curl_easy_cleanup(curl);
	
	return status;
}








///////////////////////////////////////////////////////////////////////
// paged iteration

#define MAX_REQUEST 100

typedef struct {
	string_t * json;
	CURL * curl;
} json_curl_t;

typedef void make_paged_request_url_func(char * buf, const char * user_id, unsigned offset, unsigned limit);

typedef int paged_request_item_func(cJSON * body, cJSON * item, void * custom);

int iterate_paged_items(
	const char * user_id,
	unsigned offset, unsigned max,
	const char * item_array_name,
	make_paged_request_url_func url_func,
	paged_request_item_func item_func, void * item_func_custom,
	string_t * json, CURL * curl, int deleting
	) {
	int status = 0;
	
	cJSON * cjson = NULL;
	
	char url[512];
	
	unsigned real_offset = offset;
	unsigned remaining = max;
	unsigned total_items = UINT_MAX;
	while (remaining > 0) {
		// make this request
		unsigned limit = min(remaining, MAX_REQUEST);
		url_func(url, user_id, offset, limit);
		printf("Getting items %u-%u...", real_offset, real_offset+limit-1);
		cJSON * body = fetch_json_body(url, NULL, json, &cjson, curl);
		if (!body) {
			status = -1;
			goto cleanup;
		}
		
		// if haven't gotten total, get it
		if (total_items == UINT_MAX) {
			cJSON * total = cJSON_GetObjectItem(body, "total");
			if (!total) {
				puts("can't get total items");
				status = -1;
				goto cleanup;
			}
			printf("%d items total...", total->valueint);
			
			total_items = total->valueint;
			if (max == UINT_MAX) {
				// no max specified, reset remaining to all bookmarks after offset
				max = total_items - real_offset;
				remaining = max;
			} else if (remaining > total_items - offset) {
				// max was specified, but it was larger than the actual total
				max = total_items - real_offset;
				remaining = max;
			}
		}
		
		// iterate through items
		cJSON * items = cJSON_GetObjectItem(body, item_array_name);
		if (!items) {
			puts("can't get item list");
			status = -1;
			goto cleanup;
		}
		puts("OK");
		cJSON * item = items->child;
		unsigned items_read = 0;
		while (item) {
			int item_status = item_func(body, item, item_func_custom);
			if (item_status)
				status = item_status;
			
			item = item->next;
			items_read++;
		}
		
		if (items_read == 0) {
			puts("got 0 items! something is wrong");
			goto cleanup;
		}
		
		remaining -= items_read;
		if (!deleting)
			offset += items_read;
		real_offset += items_read;
		
		cJSON_Delete(cjson);
		cjson = NULL;
	}
	
cleanup:
	cJSON_Delete(cjson);
	
	return status;
}




typedef const char * get_cjson_item_id_func(cJSON * item);

typedef struct {
	FILE * f;
	get_cjson_item_id_func * item_id_func;
} write_item_id_t;

// unused parameter "body" is required by interface
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
int write_paged_item_id_to_file(cJSON * body, cJSON * item, void * custom) {
	write_item_id_t * data = custom;
	
	const char * id = data->item_id_func(item);
	if (!id) {
		return 1;
	}
	
	fputs(id, data->f);
	fputc('\n', data->f);
	
	return 0;
}
#pragma GCC diagnostic pop











////////////////////////////////////////////////////////////////////////
// illusts

void make_illust_request_url(char * buf, const char * id) {
	sprintf(buf,
		BASE
		"/illust/%s"
		"?lang=" LANG
		"&version=" VERSION
		,id
	);
}

void make_illust_pages_request_url(char * buf, const char * id) {
	sprintf(buf,
		BASE
		"/illust/%s/pages"
		"?lang=" LANG
		"&version=" VERSION
		,id
	);
}

void make_illust_ugoira_meta_request_url(char * buf, const char * id) {
	sprintf(buf,
		BASE
		"/illust/%s/ugoira_meta"
		"?lang=" LANG
		"&version=" VERSION
		,id
	);
}

void make_illust_referer_url(char * buf, const char * id) {
	sprintf(buf, "https://www.pixiv.net/en/artworks/%s", id);
}

int write_illust(char * path_buf, char * filename_buf, const char * id, string_t * json, CURL * curl) {
	int status = -1;
	
	cJSON * cjson = NULL;
	
	char url[512];
	char referer[512];
	
	// get illust info
	printf("Getting illust %s info...", id);
	make_illust_request_url(url, id);
	make_illust_referer_url(referer, id);
	cJSON * body = fetch_json_body(url, referer, json, &cjson, curl);
	if (!body) {
		goto cleanup;
	}
	
	cJSON * illustType = cJSON_GetObjectItem(body, "illustType");
	if (!illustType) {
		puts("can't get illust type");
		goto cleanup;
	}
	int illust_type = illustType->valueint;
	switch (illust_type) {
		case 0:
			puts("Standard illust");
			break;
		case 1:
			puts("Manga");
			break;
		case 2:
			puts("Ugoira");
			break;
		default:
			printf("Invalid type %d: assuming standard illust\n", illust_type);
			break;
	}
	cJSON_Delete(cjson);
	cjson = NULL;
	
	
	// get illust
	int failed_items = 0;
	if (illust_type != 2) {
		// is not ugoira
		printf("Getting %s pages...", id);
		make_illust_pages_request_url(url, id);
		cJSON * body = fetch_json_body(url, NULL, json, &cjson, curl);
		if (!body) {
			goto cleanup;
		}
		
		puts("OK");
		int page = 0;
		cJSON * item = body->child;
		while (item) {
			failed_items++;
			printf("Getting %s page %u...", id, page);
			cJSON * urls = cJSON_GetObjectItem(item, "urls");
			if (!urls) {
				puts("can't get urls");
				goto page_failed;
			}
			cJSON * original = cJSON_GetObjectItem(urls, "original");
			char * original_str = cJSON_GetStringValue(original);
			if (!original_str) {
				puts("can't get original url");
				goto page_failed;
			}
			
			if (!save_file_from_url(path_buf, filename_buf, original_str, curl))
				failed_items--;
			
page_failed:
			page++;
			item = item->next;
		}
	} else {
		// is ugoira
		printf("Getting %s ugoira meta...", id);
		make_illust_ugoira_meta_request_url(url, id);
		cJSON * body = fetch_json_body(url, NULL, json, &cjson, curl);
		if (!body) {
			goto cleanup;
		}
		
		cJSON * originalSrc = cJSON_GetObjectItem(body, "originalSrc");
		char * original_src_str = cJSON_GetStringValue(originalSrc);
		if (!original_src_str) {
			puts("can't get original url");
			goto cleanup;
		}
		
		if (save_file_from_url(path_buf, filename_buf, original_src_str, curl))
			failed_items++;
	}
	
	if (!failed_items)
		status = 0;
	
cleanup:
	cJSON_Delete(cjson);
	return status;
}











///////////////////////////////////////////////////////////////////////////
// user illusts

void make_user_illusts_request_url(char * buf, const char * user_id) {
	sprintf(buf,
		BASE
		"/user/%s/profile/all"
		"?lang=" LANG
		"&version=" VERSION
		,user_id
	);
}



int write_user_illust_ids_to_file(FILE * f, const char * user_id, string_t * json, CURL * curl) {
	int status = -1;
	
	cJSON * cjson = NULL;
	
	char url[512];
	
	// get user info
	printf("Getting user %s illusts...", user_id);
	make_user_illusts_request_url(url, user_id);
	cJSON * body = fetch_json_body(url, NULL, json, &cjson, curl);
	if (!body)
		goto cleanup;
	
	status = 0;
	
	// try writing illusts
	cJSON * illusts = cJSON_GetObjectItem(body, "illusts");
	if (illusts) {
		unsigned total_illusts = 0;
		
		cJSON * o = illusts->child;
		while (o) {
			fputs(o->string, f);
			fputc('\n', f);
			
			total_illusts++;
			o = o->next;
		}
		
		printf("%u total illusts...", total_illusts);
	} else {
		printf("Can't get illusts...");
		status = -2;
	}
	
	// try writing manga
	cJSON * manga = cJSON_GetObjectItem(body, "manga");
	if (manga) {
		unsigned total_manga = 0;
		
		cJSON * o = manga->child;
		while (o) {
			fputs(o->string, f);
			fputc('\n', f);
			
			total_manga++;
			o = o->next;
		}
		
		printf("%u total manga\n", total_manga);
	} else {
		puts("Can't get manga");
		status = -3;
	}
	
cleanup:
	cJSON_Delete(cjson);
	return status;
}


















//////////////////////////////////////////////////////////////////////////
// user bookmarks

void make_bookmarks_request_url(char * buf, const char * user_id, unsigned offset, unsigned limit) {
	sprintf(buf,
		BASE
		"/user/%s/illusts/bookmarks"
		"?tag="
		"&offset=%u"
		"&limit=%u"
		"&rest=show"
		"&lang=" LANG
		"&version=" VERSION
		,user_id
		,offset
		,limit
	);
}


const char * get_cjson_bookmark_illust_id(cJSON * item) {
	cJSON * id = cJSON_GetObjectItem(item, "id");
	const char * id_value = cJSON_GetStringValue(id);
	
	if (!id_value)
		puts("Can't get illust ID! It was probably deleted.");
	return id_value;
}

const char * get_cjson_bookmark_id(cJSON * item) {
	cJSON * bookmarkData = cJSON_GetObjectItem(item, "bookmarkData");
	cJSON * id = cJSON_GetObjectItem(bookmarkData, "id");
	const char * id_value = cJSON_GetStringValue(id);
	
	if (!id_value)
		puts("Can't get bookmark ID! Are these YOUR bookmarks?");
	return id_value;
}

int add_bookmark(const char * id, string_t * json, CURL * curl) {
	printf("Adding %s to bookmarks...", id);
	
	cJSON * cjson = NULL;
	
	char request[128];
	sprintf(request, "{\"illust_id\":\"%s\",\"restrict\":0,\"comment\":\"\",\"tags\":[]}", id);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request);
	
	cJSON * body = fetch_json_body(BASE "/illusts/bookmarks/add", NULL, json, &cjson, curl);
	cJSON_Delete(cjson);
	if (!body)
		return 1;
	
	puts("OK");
	return 0;
}

int delete_bookmark(const char * id, string_t * json, CURL * curl) {
	printf("Deleting %s from bookmarks...", id);
	
	cJSON * cjson = NULL;
	
	char request[128];
	sprintf(request, "bookmark_id=%s", id);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request);
	
	cJSON * body = fetch_json_body(BASE "/illusts/bookmarks/delete", NULL, json, &cjson, curl);
	cJSON_Delete(cjson);
	if (!body)
		return 1;
	
	puts("OK");
	return 0;
}



const char * fetch_bookmark_id_from_illust(const char * illust_id, string_t * json, CURL * curl) {
	cJSON * cjson = NULL;
	static char id_buf[64];
	const char * out = NULL;
	
	char url[512];
	char referer[512];
	
	printf("Getting illust %s bookmark id...", illust_id);
	make_illust_request_url(url, illust_id);
	make_illust_referer_url(referer, illust_id);
	cJSON * body = fetch_json_body(url, referer, json, &cjson, curl);
	if (!body)
		goto cleanup;
	
	cJSON * bookmarkData = cJSON_GetObjectItem(body, "bookmarkData");
	cJSON * id = cJSON_GetObjectItem(bookmarkData, "id");
	const char * id_value = cJSON_GetStringValue(id);
	if (id_value) {
		strcpy(id_buf, id_value);
		out = id_buf;
		puts("OK");
	} else {
		puts("Can't get bookmark ID! Did you really bookmark this?");
	}
	
cleanup:
	cJSON_Delete(cjson);
	return out;
}


int write_bookmark_id_from_illust_to_file(FILE * f, const char * illust_id, string_t * json, CURL * curl) {
	const char * id = fetch_bookmark_id_from_illust(illust_id, json, curl);
	
	if (id) {
		fputs(id, f);
		fputc('\n', f);
		return 0;
	} else {
		return 1;
	}
}

int delete_bookmark_from_illust(const char * illust_id, string_t * json, CURL * curl, CURL * post_curl) {
	const char * id = fetch_bookmark_id_from_illust(illust_id, json, curl);
	
	if (id) {
		return delete_bookmark(id, json, post_curl);
	} else {
		return 1;
	}
}








/////////////////////////////////////////////////////////////////////////
// following

void make_following_request_url(char * buf, const char * user_id, unsigned offset, unsigned limit) {
	sprintf(buf,
		BASE
		"/user/%s/following"
		"?offset=%u"
		"&limit=%u"
		"&rest=show"
		"&tag="
		"&acceptingRequests=0"
		"&lang=" LANG
		"&version=" VERSION
		,user_id
		,offset
		,limit
	);
}


const char * get_cjson_following_user_id(cJSON * item) {
	cJSON * userId = cJSON_GetObjectItem(item, "userId");
	const char * id_value = cJSON_GetStringValue(userId);
	
	if (!id_value)
		puts("Can't get user ID!");
	return id_value;
}


int follow_user(const char * id, string_t * response, CURL * curl) {
	printf("Following user %s...", id);
	
	char request[128];
	sprintf(request, "mode=add&type=user&user_id=%s&tag=&restrict=0&format=json", id);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request);
	
	char referer[128];
	sprintf(referer, "https://www.pixiv.net/" LANG "/users/%s", id);
	
	response->size = 0;
	int status = fetch_to_string("https://www.pixiv.net/bookmark_add.php", referer, response, curl);
	
	if (!status && (response->size != 2 || memcmp(response->data, "[]", 2))) {
		puts("failed");
		status = 1;
	} else if (!status) {
		puts("OK");
	}
	
	return status;
}

int unfollow_user(const char * id, string_t * json, CURL * curl) {
	printf("Unfollowing user %s...", id);
	
	char request[128];
	sprintf(request, "mode=del&type=bookuser&id=%s", id);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request);
	
	char referer[128];
	sprintf(referer, "https://www.pixiv.net/" LANG "/users/%s", id);
	
	json->size = 0;
	cJSON * cjson = fetch_json("https://www.pixiv.net/rpc_group_setting.php", referer, json, curl);
	if (!cjson) {
		return 1;
	}
	
	if (cJSON_IsTrue(cJSON_GetObjectItem(cjson, "error"))) {
		puts(cJSON_GetStringValue(cJSON_GetObjectItem(cjson, "message")));
		return 1;
	}
	
	puts("OK");
	return 0;
}















/////////////////////////////////////////////////////////////////////////////
// main

void print_usage() {
	puts(
		"usage: pixiv [option]... command [argument]..."
		"\n"
		"\ncommands/arguments:"
		"\n\t--get-illust illust-id..."
		"\n"
		"\n\t--get-user-illusts user-id..."
		"\n"
		"\n\t--bookmark illust-id..."
		"\n\t--delete-bookmark illust-id..."
		"\n\t--delete-bookmark-id bookmark-id..."
		"\n"
		"\n\t--get-bookmark-id illust-id..."
		"\n\t--get-bookmarks user-id..."
		"\n\t--get-bookmark-ids user-id..."
		"\n"
		"\n\t--follow user-id..."
		"\n\t--unfollow user-id..."
		"\n"
		"\n\t--get-following user-id..."
		"\n"
		"\noptions:"
		"\n\t-? --help"
		"\n"
		"\n\t-i in-file-name"
		"\n\t-o out-file-name"
		"\n\t-P path-name"
		"\n"
		"\n\t--cookies cookies-file"
		//"\n\t--user-agent user-agent"
		//"\n\t--api-version version-str" //not supported. does this even do anything?
		//"\n\t--api-lang lang-str" //not supported so we don't get blindsided by japanese error messages
		"\n"
		"\n\t--offset first-index"
		"\n\t--max max-fetched"
	);
}


enum {
	CMD_NONE = 0,
	
	CMD_GET_ILLUST,
	
	CMD_GET_USER_ILLUSTS,
	
	CMD_BOOKMARK,
	CMD_DELETE_BOOKMARK,
	CMD_DELETE_BOOKMARK_ID,
	
	CMD_GET_BOOKMARK_ID,
	CMD_GET_BOOKMARKS,
	CMD_GET_BOOKMARK_IDS,
	
	CMD_FOLLOW,
	CMD_UNFOLLOW,
	
	CMD_GET_FOLLOWING,
	
	CMD_MAX
};

enum {
	OPT_HELP = '?',
	
	OPT_IN_FILENAME = 'i',
	OPT_OUT_FILENAME = 'o',
	OPT_PATH_NAME = 'P',
	
	OPT_COOKIES_FILE = 0x80,
	//OPT_USER_AGENT,
	//OPT_API_VERSION,
	//OPT_API_LANG,
	
	OPT_OFFSET,
	OPT_MAX,
};

const char * short_opts = "i:o:P:?";

const struct option long_opts[] = {
	// "command" options
	{"get-illust", no_argument, NULL, CMD_GET_ILLUST},
	
	{"get-user-illusts", no_argument, NULL, CMD_GET_USER_ILLUSTS},
	
	{"bookmark", no_argument, NULL, CMD_BOOKMARK},
	{"delete-bookmark", no_argument, NULL, CMD_DELETE_BOOKMARK},
	{"delete-bookmark-id", no_argument, NULL, CMD_DELETE_BOOKMARK_ID},
	
	{"get-bookmarks", no_argument, NULL, CMD_GET_BOOKMARKS},
	{"get-bookmark-id", no_argument, NULL, CMD_GET_BOOKMARK_ID},
	{"get-bookmark-ids", no_argument, NULL, CMD_GET_BOOKMARK_IDS},
	
	{"follow", no_argument, NULL, CMD_FOLLOW},
	{"unfollow", no_argument, NULL, CMD_UNFOLLOW},
	
	{"get-following", no_argument, NULL, CMD_GET_FOLLOWING},
	
	// "option" options
	{"help", required_argument, NULL, OPT_HELP},
	
	{"cookies", required_argument, NULL, OPT_COOKIES_FILE},
	//{"user-agent", required_argument, NULL, OPT_USER_AGENT},
	//{"api-version", required_argument, NULL, OPT_API_VERSION},
	//{"api-lang", required_argument, NULL, OPT_API_LANG},
	
	{"offset", required_argument, NULL, OPT_OFFSET},
	{"max", required_argument, NULL, OPT_MAX},
	
	{0, 0, 0, 0}
};


int main(int argc, char * argv[]) {
	if (argc <= 1) {
		print_usage();
		return EXIT_FAILURE;
	}
	
	//////////////////////////////////// parse arguments
	string_t in_filenames = {0,0,0};
	const char * out_filename = NULL;
	const char * path_name = NULL;
	
	const char * cookies_filename = NULL;
	
	unsigned offset = 0;
	unsigned max = UINT_MAX;
	
	int cmd = CMD_NONE;
	
	while (1) {
		int option_index = 0;
		int c = getopt_long(argc, argv, short_opts, long_opts, &option_index);
		if (c == -1)
			break;
		
		if (c > CMD_NONE && c < CMD_MAX) {
			if (cmd) {
				printf("Duplicate command %s\n", long_opts[option_index].name);
				return EXIT_FAILURE;
			} else {
				cmd = c;
			}
		} else if (c == OPT_IN_FILENAME) {
			if (!in_filenames.data)
				string_init(&in_filenames, 0x200);
			string_append_chars(&in_filenames, optarg, strlen(optarg)+1);
		} else if (c == OPT_OUT_FILENAME) {
			if (out_filename) {
				printf("Duplicate out file name %s\n", optarg);
				return EXIT_FAILURE;
			} else {
				out_filename = optarg;
			}
		} else if (c == OPT_PATH_NAME) {
			if (path_name) {
				printf("Duplicate path name %s\n", optarg);
				return EXIT_FAILURE;
			} else {
				path_name = optarg;
			}
		}	else if (c == OPT_COOKIES_FILE) {
			if (cookies_filename) {
				printf("Duplicate cookies file name %s\n", optarg);
				return EXIT_FAILURE;
			} else {
				cookies_filename = optarg;
			}
		} else if (c == OPT_OFFSET) {
			offset = strtoul(optarg, NULL, 0);
		} else if (c == OPT_MAX) {
			max = strtoul(optarg, NULL, 0);
		} else {
			print_usage();
			return EXIT_FAILURE;
		}
	}
	
	if (cmd == CMD_NONE) {
		puts("No command specified");
		return EXIT_FAILURE;
	}
	
	//////////////////////////////////////////// init libcurl
	int status = curl_global_init(CURL_GLOBAL_DEFAULT);
	if (status) {
		printf("Can't init curl global: %s\n", curl_easy_strerror(status));
		return status;
	}
	
	//////////////////////////////////////////// command variables
	int argi = optind; // current index to argv
	size_t in_filenames_index = 0; // current index to arg-file names
	FILE * argf = NULL; // current input arg-file
	
	char * path_buf = NULL; // output filename buffer (for commands which read/write many files)
	char * filename_buf = NULL; // just the filename part of the full path
	
	FILE * of = NULL; // current output file (for commands which write only ONE outfile)
	
	CURL * curl = NULL; // command-wide curl object for http get
	
	CURL * post_curl = NULL; // command-wide curl object for http post
	struct curl_slist * slist = NULL; // command-wide string list for http post request
	
	string_t response = {0,0,0}; // string holding response data (usually json)
	
	//////////////////////////////////////////// get cookies
	if (cookies_filename) {
		status = read_cookies_from_file(cookies_filename);
		if (status)
			return status;
	} else {
		puts("WARNING: no cookies file specified, this will probably fail");
	}
	
	//////////////////////////////////////////// do command pre-initialization
	
	////// initialize path buffer for commands which batch-access files
	switch (cmd) {
		case CMD_GET_ILLUST:
		{
			if (!path_name)
				path_name = ".";
			size_t path_len = strlen(path_name);
			path_buf = malloc(path_len+128);
			memcpy(path_buf,path_name,path_len);
			path_buf[path_len] = '/';
			filename_buf = &path_buf[path_len+1];
		}
	}
	
	////// create a get curl object for commands which do http get
	switch (cmd) {
		case CMD_GET_ILLUST:
		case CMD_GET_USER_ILLUSTS:
		case CMD_DELETE_BOOKMARK: // needed to get bookmark id from illust info
		case CMD_GET_BOOKMARK_ID:
		case CMD_GET_BOOKMARKS:
		case CMD_GET_BOOKMARK_IDS:
		case CMD_GET_FOLLOWING:
		{
			curl = make_curl();
		}
	}
	
	////// create a post curl object for commands which do http post
	switch (cmd) {
		case CMD_BOOKMARK:
		case CMD_DELETE_BOOKMARK:
		case CMD_DELETE_BOOKMARK_ID:
		case CMD_FOLLOW:
		case CMD_UNFOLLOW:
		{
			status = fetch_csrf_token(); // also requires csrf token
			if (status) {
				goto cleanup;
			}
			
			post_curl = make_curl_post(
				cmd == CMD_BOOKMARK ? CONTENT_TYPE_JSON : CONTENT_TYPE_NORMAL,
				&slist
			);
		}
	}
	
	////// create string to hold response
	string_init(&response, 0x2000);
	
	////// open output file for commands which need one
	switch (cmd) {
		case CMD_GET_USER_ILLUSTS:
		case CMD_GET_BOOKMARK_ID:
		case CMD_GET_BOOKMARKS:
		case CMD_GET_BOOKMARK_IDS:
		case CMD_GET_FOLLOWING:
		{
			if (out_filename) {
				of = fopen(out_filename,"wb");
				if (!of) {
					int en = errno;
					printf("%s open: %s\n", out_filename, strerror(en));
					status = en;
					goto cleanup;
				}
			} else {
				puts("This command requires an output filename");
				status = EXIT_FAILURE;
				goto cleanup;
			}
		}
	}
	
	/////////////////////////////////////////// main command procedure
	unsigned tries = 0;
	unsigned fails = 0;
	while (1) {
		//// get the next argument, either from file or command line
		char * current_arg = NULL;
		char arg_read_buf[64];
		while (!current_arg) {
			if (argf) {
				// currently reading from file
				while (1) {
					if (!fgets(arg_read_buf, 64, argf)) {
						if (ferror(argf)) {
							// read error
							int en = errno;
							printf("%s read: %s\n", &in_filenames.data[in_filenames_index], strerror(en));
							fclose(argf);
							argf = NULL;
							status = en;
							goto cleanup;
						}
						// eof
						fclose(argf);
						argf = NULL;
						in_filenames_index += strlen(&in_filenames.data[in_filenames_index])+1;
						break;
					}
					
					// got line, is blank?
					size_t len = strlen(arg_read_buf);
					if (!len)
						continue;
					else if (arg_read_buf[len-1] == '\n')
						arg_read_buf[--len] = '\0';
					if (!len)
						continue;
					
					current_arg = arg_read_buf;
					break;
				}
			} else if (in_filenames_index < in_filenames.size) {
				// need a new arg file
				argf = fopen(&in_filenames.data[in_filenames_index], "r");
				if (!argf) {
					int en = errno;
					printf("%s open: %s\n", &in_filenames.data[in_filenames_index], strerror(en));
					status = en;
					goto cleanup;
				}
			} else if (argi < argc) {
				// get arg from command line
				current_arg = argv[argi++];
			} else {
				// nothing left
				break;
			}
		}
		
		//// broke out of loop with no argument? if so, we're done
		if (!current_arg)
			break;
		
		//// execute command for this argument
		tries++;
		int cmd_status = 1;
		switch (cmd) {
			case CMD_GET_ILLUST:
				cmd_status = write_illust(path_buf, filename_buf, current_arg, &response, curl);
				break;
			
			case CMD_GET_USER_ILLUSTS:
				cmd_status = write_user_illust_ids_to_file(of, current_arg, &response, curl);
				break;
			
			case CMD_BOOKMARK:
				cmd_status = add_bookmark(current_arg, &response, post_curl);
				break;
			case CMD_DELETE_BOOKMARK:
				cmd_status = delete_bookmark_from_illust(current_arg, &response, curl, post_curl);
				break;
			case CMD_DELETE_BOOKMARK_ID:
				cmd_status = delete_bookmark(current_arg, &response, post_curl);
				break;
			
			case CMD_GET_BOOKMARK_ID:
				cmd_status = write_bookmark_id_from_illust_to_file(
					of, current_arg, &response, curl);
				break;
			case CMD_GET_BOOKMARKS: {
				write_item_id_t data = {of, get_cjson_bookmark_illust_id};
				cmd_status = iterate_paged_items(current_arg, offset, max,
					"works", make_bookmarks_request_url,
					write_paged_item_id_to_file, &data,
					&response, curl, 0);
				break;
			}
			case CMD_GET_BOOKMARK_IDS: {
				write_item_id_t data = {of, get_cjson_bookmark_id};
				cmd_status = iterate_paged_items(current_arg, offset, max,
					"works", make_bookmarks_request_url,
					write_paged_item_id_to_file, &data,
					&response, curl, 0);
				break;
			}
			
			case CMD_FOLLOW:
				cmd_status = follow_user(current_arg, &response, post_curl);
				break;
			case CMD_UNFOLLOW:
				cmd_status = unfollow_user(current_arg, &response, post_curl);
				break;
			
			case CMD_GET_FOLLOWING: {
				write_item_id_t data = {of, get_cjson_following_user_id};
				cmd_status = iterate_paged_items(current_arg, offset, max,
					"users", make_following_request_url,
					write_paged_item_id_to_file, &data,
					&response, curl, 0);
				break;
			}
		}
		if (cmd_status)
			fails++;
	}
	//printf("Attempted %u, %u failed\n", tries, fails);
	status = fails;
	
	/////////////////////////////////////////// cleanup
cleanup:
	if (argf)
		fclose(argf);
	
	free(path_buf);
	
	if (of)
		fclose(of);
	
	curl_easy_cleanup(curl);
	
	curl_easy_cleanup(post_curl);
	curl_slist_free_all(slist);
	
	string_free(&response);

	curl_global_cleanup();
	return status;
}
