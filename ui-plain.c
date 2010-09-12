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

static void print_gitlink(const unsigned char *sha1, const char *path)
{
	char *slash;
	while ((slash = strchr(path, '/')))
		path = slash + 1;
	ctx.page.mimetype = "text/plain";
	ctx.page.filename = fmt("%s", path);
	ctx.page.size = strlen("gitlink: ") + 40;
	ctx.page.etag = sha1_to_hex(sha1);
	cgit_print_http_headers(&ctx);
	html("gitlink: ");
	html(ctx.page.etag);
}

static void print_object(const unsigned char *sha1, const char *path)
{
	enum object_type type;
	char *buf, *ext, *slash;
	unsigned long size;
	struct string_list_item *mime;

	if (ends_with_slash() > 0) {
		not_found("Not a directory: %s", path);
		return;
	}

	while ((slash = strchr(path, '/')))
		path = slash + 1;

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
}

static void print_dir(const unsigned char *sha1, const char *path)
{
	struct tree_desc desc;
	struct name_entry entry;
	struct tree *tree;

	if (!ensure_slash())
		return;

	if (path[0])
		path = fmt("%s/%s/", ctx.repo->name, path);
	else
		path = fmt("%s/", ctx.repo->name);

	tree = lookup_tree(sha1);
	if (!tree || parse_tree(tree)) {
		not_found("Invalid tree");
		return;
	}

	ctx.page.etag = sha1_to_hex(sha1);
	cgit_print_http_headers(&ctx);
	html("<html><head><title>");
	html_txt(path);
	html("</title></head>\n<body>\n <h2>");
	html_txt(path);
	html("</h2>\n <ul>\n");
	if (path[1])
	      html("  <li><a href=\"../\">../</a></li>\n");

	init_tree_desc(&desc, tree->buffer, tree->size);
	while (tree_entry(&desc, &entry)) {
		char *url;
		if (S_ISDIR(entry.mode))
			url = fmt("%s/", entry.path);
		else
			url = fmt("%s", entry.path);
		html("  <li><a href='");
		html_url_path(url);
		html("'>");
		html_txt(url);
		html("</a></li>\n");
	}

	html(" </ul>\n</body></html>\n");
}

void cgit_print_plain(struct cgit_context *ctx)
{
	const char *rev = ctx->qry.sha1;
	unsigned char sha1_rev[20], sha1_object[20];
	char *pathname = "", *errmsg = NULL;
	int mode = 0, flags = 0;

	if (!rev)
		rev = ctx->qry.head;
	if (get_sha1(rev, sha1_rev)) {
		not_found("Ref not found: %s", rev);
		return;
	}
	if (ctx->qry.path)
		pathname = ctx->qry.path;
	if (ctx->cfg.enable_symlink_traversal)
		flags |= FOLLOW_SYMLINKS;

	if (cgit_find_object_by_path(sha1_rev, pathname, flags, sha1_object,
				     &mode, &errmsg))
		not_found("%s", errmsg);
	else if (S_ISDIR(mode))
		print_dir(sha1_object, pathname);
	else if (S_ISREG(mode) || S_ISLNK(mode))
		print_object(sha1_object, pathname);
	else if (S_ISGITLINK(mode))
		print_gitlink(sha1_object, pathname);
	else if (mode >= 0)
		not_found("Invalid mode: %d", mode);

	free(errmsg);
}
