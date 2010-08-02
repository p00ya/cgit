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

#define MAX_SYMLINKS 10
#define MAX_DEPTH 128

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
	if (!ctx.qry.raw)
		return -1;
	n = strlen(ctx.qry.raw);
	return (ctx.qry.raw[n-1] == '/');
}

static int ensure_slash()
{
	char *path;
	if (!ctx.cfg.virtual_root || !ctx.env.script_name ||
	    !ctx.env.path_info || ends_with_slash())
		return 1;
	path = fmt("%s%s%s%s/", cgit_httpscheme(), cgit_hosturl(),
		   ctx.env.script_name, ctx.env.path_info);
	html("Status: 301 Moved Permanently\n"
	     "Location: ");
	html(path);
	html("\n"
	     "Content-Type: text/html; charset=UTF-8\n"
	     "\n"
	     "<html><head><title>301 Moved Permanently</title></head>\n"
	     "<body><h1>404 Not Found</h1>\n"
	     "<p>The document has moved <a href='");
	html_attr(path);
	html("'>here</a>.\n"
	     "</body></html>\n");
	return 0;
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
		path = fmt("/%s/", path);
	else
		path = "/";

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

static int follow_symlink(const unsigned char *sha1, struct strbuf *path,
			  int start, const char *pathname)
{
	enum object_type type;
	unsigned long size, pathlen;
	char *value, *slash;

	type = sha1_object_info(sha1, &size);
	if (type == OBJ_BAD) {
		not_found("Bad object: %s", sha1_to_hex(sha1));
		return -1;
	}

	value = read_sha1_file(sha1, &type, &size);
	if (!value) {
		not_found("Object not found: %s", sha1_to_hex(sha1));
		return -1;
	}
	if (size == 0) {
		not_found("Zero-length symbolic link");
		return -1;
	}
	if (value[0] == '/') {
		not_found("Absolute symbolic link: %s -> %s", pathname, value);
		return -1;
	}

	slash = strchr(path->buf + start, '/');
	if (slash)
		pathlen = slash - (path->buf + start);
	else
		pathlen = strlen(path->buf + start);
	strbuf_splice(path, start, pathlen, value, size);

	return 0;
}

/* Remove "" and "." path components.
 * Leave (single) trailing slash if it exists.
 * 'dst' and 'src' may overlap, as long as dst <= src. */
static void normalize_path(char *dst, const char *src)
{
	while (*src == '/')
		src++;
	while (*src) {
		char *slash = strchr(src, '/');
		size_t len;
		if (slash)
			len = slash - src;
		else
			len = strlen(src);
		if (!((len == 0) ||
		      (len == 1 && src[0] == '.') ||
		      (len == 1 && src[0] == '.' && src[1] == '.')))
		{
			memmove(dst, src, len);
			dst += len;
			if (slash)
				*dst++ = '/';
		}
		src += len;
		while (*src == '/')
			src++;
	}
	*dst++ = '\0';
}

/* return 1 if the first path component of 'path' begins with 'prefix' */
static int path_prefix(const char *path, const char *prefix)
{
	char *slash = strchr(path, '/');
	if (slash) {
		size_t len = slash - path;
		size_t prefix_len = strlen(prefix);
		return (len == prefix_len) && !strncmp(path, prefix, len);
	} else
		return !strcmp(path, prefix);
}

static int walk_tree(const unsigned char *sha1_root, struct strbuf *pathbuf,
		     unsigned char *sha1_out, int maxlinks)
{
	static struct tree *trees[MAX_DEPTH];
	static struct name_entry entries[MAX_DEPTH];
	char *p;
	int depth;

	if (maxlinks <= 0) {
		not_found("Too many symbolic links");
		return -1;
	}

	memset(trees, 0, sizeof trees);
	trees[0] = parse_tree_indirect(sha1_root);
	if (!trees[0]) {
		not_found("Invalid sha1: %s", sha1_to_hex(sha1_root));
		return -1;
	}
	entries[0].sha1 = trees[0]->object.sha1;
	entries[0].path = "";
	entries[0].mode = S_IFDIR;

	normalize_path(pathbuf->buf, pathbuf->buf);
	strbuf_setlen(pathbuf, strlen(pathbuf->buf));

	p = pathbuf->buf;
	if (!strcmp(p, "") || !strcmp(p, "/")) {
		memcpy(sha1_out, entries[0].sha1, 20);
		return entries[0].mode;
	}
	for (depth = 0; depth < MAX_DEPTH-1; depth++) {
		struct tree *tree;
		struct tree_desc desc;
		struct name_entry *entry;
		int found;
		char *slash;


		while (path_prefix(p, "..")) {
			if (depth <= 0) {
				not_found("Symbolic link outside top level: %s",
					  pathbuf->buf);
				return -1;
			}
			trees[depth] = NULL;
			depth--;
			if (p[2] == '\0' || (p[2] == '/' && p[3] == '\0')) {
				memcpy(sha1_out, entries[depth].sha1, 20);
				return entries[depth].mode;
			}
			p += 3;
		}

		tree = trees[depth];
		if (!tree) {
			tree = lookup_tree(entries[depth].sha1);
			if (!tree || parse_tree(tree)) {
				not_found("Invalid tree");
				return -1;
			}
			trees[depth] = tree;
		}
		init_tree_desc(&desc, tree->buffer, tree->size);


		entry = &entries[depth+1];
		found = 0;
		while (tree_entry(&desc, entry)) {
			if (path_prefix(p, entry->path)) {
				found = 1;
				break;
			}
		}

		slash = strchr(p, '/');
		if (!found) {
			not_found("File not found");
			return -1;
		} else if (S_ISLNK(entry->mode)) {
			if (follow_symlink(entry->sha1, pathbuf,
					   p-pathbuf->buf, entry->path))
				return -1;
			return walk_tree(sha1_root, pathbuf, sha1_out, maxlinks-1);
		} else if (!slash) {
			memcpy(sha1_out, entry->sha1, 20);
			return entry->mode;
		} else if (!S_ISDIR(entry->mode)) {
			not_found("Not a directory: %s",
				  pathbuf->buf);
			return -1;
		} else
			p = slash + 1;
	}

	not_found("Directory nesting too deep");
	return -1;
}

void cgit_print_plain(struct cgit_context *ctx)
{
	const char *rev = ctx->qry.sha1;
	struct strbuf pathbuf = STRBUF_INIT;
	unsigned char sha1_rev[20], sha1_object[20];
	char *pathname;
	int mode;

	if (!rev)
		rev = ctx->qry.head;
	if (get_sha1(rev, sha1_rev)) {
		not_found("Ref not found: %s", rev);
		return;
	}

	if (ctx->qry.path)
		strbuf_addstr(&pathbuf, ctx->qry.path);
	pathname = xstrdup(pathbuf.buf);

	mode = walk_tree(sha1_rev, &pathbuf, sha1_object, MAX_SYMLINKS);
	if (S_ISDIR(mode))
		print_dir(sha1_object, pathname);
	else if (S_ISREG(mode))
		print_object(sha1_object, pathname);
	else if (mode >= 0)
		not_found("Invalid mode: %d", mode);

	strbuf_release(&pathbuf);
	free(pathname);
}
