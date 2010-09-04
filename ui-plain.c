/* ui-plain.c: functions for output of plain blobs by path
 *
 * Copyright (C) 2008 Lars Hjemli
 *
 * Licensed under GNU General Public License v2
 *   (see COPYING for full license text)
 */

#include "cgit.h"
#include "html.h"
#include "ui-shared.h"

int match_baselen;
int match;

__attribute__((format (printf,1,2)))
static void not_found(const char *format, ...);
static void not_found(const char *format, ...)
{
	html("Status: 404 Not Found\n"
	     "Content-Type: text/html; charset=UTF-8\n\n"
	     "<html>\n<head>\n<title>404 Not Found</title>\n</head>\n"
	     "<body>\n<h1>404 Not Found</h1>\n");
	if (format) {
		static char buf[65536];
		va_list args;
		va_start(args, format);
		vsnprintf(buf, sizeof(buf), format, args);
		va_end(args);
		html("<p>");
		html_txt(buf);
		html("</p>\n");
	}
	html("</body>\n</html>\n");
}

static int ends_with_slash()
{
	size_t n;
	if (!ctx.cfg.virtual_root || !ctx.env.path_info)
		return -1;
	n = strlen(ctx.env.path_info);
	return (ctx.env.path_info[n-1] == '/');
}

static void print_self_with_slash(void (*f)(const char *))
{
	f(cgit_httpscheme());
	f(cgit_hosturl());
	f(ctx.env.script_name);
	f(ctx.env.path_info);
	f("/");
	if (ctx.qry.sha1) {
		f("?id=");
		html_url_arg(ctx.qry.sha1);
	} else if (ctx.qry.head && strcmp(ctx.qry.head, ctx.repo->defbranch)) {
		f("?h=");
		html_url_arg(ctx.qry.head);
	}
}

static int ensure_slash()
{
	if (ends_with_slash() || !ctx.env.script_name)
		return 1;
	html("Status: 301 Moved Permanently\n"
	     "Location: ");
	print_self_with_slash(html);
	html("\n"
	     "Content-Type: text/html; charset=UTF-8\n"
	     "\n"
	     "<html><head><title>301 Moved Permanently</title></head>\n"
	     "<body><h1>404 Not Found</h1>\n"
	     "<p>The document has moved <a href='");
	print_self_with_slash(html_attr);
	html("'>here</a>.\n"
	     "</body></html>\n");
	return 0;
}

static void print_object(const unsigned char *sha1, const char *path)
{
	enum object_type type;
	char *buf, *ext;
	unsigned long size;
	struct string_list_item *mime;

	type = sha1_object_info(sha1, &size);
	if (type == OBJ_BAD) {
		not_found("Bad object: %s", sha1_to_hex(sha1));
		return;
	}

	buf = read_sha1_file(sha1, &type, &size);
	if (!buf) {
		not_found("Object not found: %s", sha1_to_hex(sha1));
		return;
	}
	ctx.page.mimetype = NULL;
	ext = strrchr(path, '.');
	if (ext && *(++ext)) {
		mime = string_list_lookup(&ctx.cfg.mimetypes, ext);
		if (mime)
			ctx.page.mimetype = (char *)mime->util;
	}
	if (!ctx.page.mimetype) {
		if (buffer_is_binary(buf, size))
			ctx.page.mimetype = "application/octet-stream";
		else
			ctx.page.mimetype = "text/plain";
	}
	ctx.page.filename = fmt("%s", path);
	ctx.page.size = size;
	ctx.page.etag = sha1_to_hex(sha1);
	cgit_print_http_headers(&ctx);
	html_raw(buf, size);
	match = 1;
}

static int print_dir(const unsigned char *sha1, const char *path,
		      const char *base)
{
	char *fullpath;
	match = 2;
	if (!ensure_slash())
		return 0;
	if (path[0] || base[0])
		fullpath = fmt("/%s%s/", base, path);
	else
		fullpath = "/";
	ctx.page.etag = sha1_to_hex(sha1);
	cgit_print_http_headers(&ctx);
	html("<html><head><title>");
	html_txt(fullpath);
	html("</title></head>\n<body>\n <h2>");
	html_txt(fullpath);
	html("</h2>\n <ul>\n");
	if (path[0] || base[0])
	      html("  <li><a href=\"../\">../</a></li>\n");
	return 1;
}

static void print_dir_entry(const unsigned char *sha1, const char *path,
			    unsigned mode)
{
	char *url;
	if (S_ISDIR(mode))
		url = fmt("%s/", path);
	else
		url = fmt("%s", path);
	html("  <li><a href='");
	html_url_path(url);
	html("'>");
	html_txt(url);
	html("</a></li>\n");
	match = 2;
}

static void print_dir_tail(void)
{
	html(" </ul>\n</body></html>\n");
}

static int walk_tree(const unsigned char *sha1, const char *base, int baselen,
		     const char *pathname, unsigned mode, int stage,
		     void *cbdata)
{
	if (baselen == match_baselen) {
		if (S_ISREG(mode))
			print_object(sha1, pathname);
		else if (S_ISDIR(mode)) {
			if (print_dir(sha1, pathname, base))
				return READ_TREE_RECURSIVE;
		}
	}
	else if (baselen > match_baselen)
		print_dir_entry(sha1, pathname, mode);
	else if (S_ISDIR(mode))
		return READ_TREE_RECURSIVE;

	return 0;
}

static int basedir_len(const char *path)
{
	char *p = strrchr(path, '/');
	if (p)
		return p - path + 1;
	return 0;
}

void cgit_print_plain(struct cgit_context *ctx)
{
	const char *rev = ctx->qry.sha1;
	unsigned char sha1[20];
	struct commit *commit;
	const char *paths[] = {ctx->qry.path, NULL};

	if (!rev)
		rev = ctx->qry.head;

	if (get_sha1(rev, sha1)) {
		not_found("Ref not found: %s", rev);
		return;
	}
	commit = lookup_commit_reference(sha1);
	if (!commit || parse_commit(commit)) {
		not_found("Invalid commit sha1: %s", sha1_to_hex(sha1));
		return;
	}
	if (!paths[0]) {
		paths[0] = "";
		match_baselen = -1;
		if (!print_dir(commit->tree->object.sha1, "", ""))
			return;
	}
	else
		match_baselen = basedir_len(paths[0]);
	read_tree_recursive(commit->tree, "", 0, 0, paths, walk_tree, NULL);
	if (!match)
		not_found("File not found");
	else if (match == 2)
		print_dir_tail();
}
