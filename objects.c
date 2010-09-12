/* objects.c: functions for retrieving git objects
 *
 * Copyright (C) 2010 Mark Lodato
 *
 * Licensed under GNU General Public License v2
 *   (see COPYING for full license text)
 */

#include "cgit.h"

#define MAX_SYMLINKS 16
#define MAX_DEPTH 128

#define ERROR(...) do { \
	*errmsg = strdup(fmt(__VA_ARGS__)); \
	return -1; \
} while (0)

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

static int follow_symlink(const unsigned char *sha1, struct strbuf *path,
			  int start, const char *pathname, char **errmsg)
{
	enum object_type type;
	unsigned long size, pathlen;
	char *value, *slash;

	type = sha1_object_info(sha1, &size);
	if (type == OBJ_BAD)
		ERROR("Bad object: %s", sha1_to_hex(sha1));

	value = read_sha1_file(sha1, &type, &size);
	if (!value)
		ERROR("Object not found: %s", sha1_to_hex(sha1));
	if (size == 0)
		ERROR("Zero-length symbolic link");
	if (value[0] == '/')
		ERROR("Absolute symbolic link: %s -> %s", pathname, value);

	slash = strchr(path->buf + start, '/');
	if (slash)
		pathlen = slash - (path->buf + start);
	else
		pathlen = strlen(path->buf + start);
	strbuf_splice(path, start, pathlen, value, size);

	return 0;
}

static int walk_tree(const unsigned char *sha1_root, struct strbuf *pathbuf,
		     unsigned char *sha1_out, int *mode, char **errmsg,
		     int maxlinks)
{
	static struct tree *trees[MAX_DEPTH];
	static struct name_entry entries[MAX_DEPTH];
	char *p;
	int depth;

	if (maxlinks == 0)
		ERROR("Too many symbolic links");

	memset(trees, 0, sizeof trees);
	trees[0] = parse_tree_indirect(sha1_root);
	if (!trees[0])
		ERROR("Invalid sha1: %s", sha1_to_hex(sha1_root));
	entries[0].sha1 = trees[0]->object.sha1;
	entries[0].path = "";
	entries[0].mode = S_IFDIR;

	normalize_path(pathbuf->buf, pathbuf->buf);
	strbuf_setlen(pathbuf, strlen(pathbuf->buf));

	p = pathbuf->buf;
	if (!strcmp(p, "") || !strcmp(p, "/")) {
		memcpy(sha1_out, entries[0].sha1, 20);
		*mode = entries[0].mode;
		return 0;
	}
	for (depth = 0; depth < MAX_DEPTH-1; depth++) {
		struct tree *tree;
		struct tree_desc desc;
		struct name_entry *entry;
		int found;
		char *slash;

		while (path_prefix(p, "..")) {
			if (depth <= 0)
				ERROR("Symbolic link outside top level: %s",
				      pathbuf->buf);
			trees[depth] = NULL;
			depth--;
			if (p[2] == '\0' || (p[2] == '/' && p[3] == '\0')) {
				memcpy(sha1_out, entries[depth].sha1, 20);
				*mode = entries[depth].mode;
				return 0;
			}
			p += 3;
		}

		tree = trees[depth];
		if (!tree) {
			tree = lookup_tree(entries[depth].sha1);
			if (!tree || parse_tree(tree))
				ERROR("Invalid tree");
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
			if (maxlinks > 0 && maxlinks < MAX_SYMLINKS)
				ERROR("Broken symblic link");
			else
				ERROR("File not found");
		} else if (S_ISLNK(entry->mode) && maxlinks > 0) {
			if (follow_symlink(entry->sha1, pathbuf,
					   p-pathbuf->buf, entry->path,
					   errmsg))
				return -1;
			return walk_tree(sha1_root, pathbuf, sha1_out,
					 mode, errmsg, maxlinks-1);
		} else if (!slash) {
			memcpy(sha1_out, entry->sha1, 20);
			*mode = entry->mode;
			return 0;
		} else if (!S_ISDIR(entry->mode)) {
			ERROR("Not a directory: %s", pathbuf->buf);
		} else
			p = slash + 1;
	}

	ERROR("Directory nesting too deep");
}

int cgit_find_object_by_path(const unsigned char *sha1_root,
			     const char *path, int flags,
			     unsigned char *sha1_out,
			     int *mode, char **errmsg)
{
	struct strbuf pathbuf = STRBUF_INIT;
	int tmp_mode = 0, maxlinks, rc;
	char *tmp_errmsg = NULL;
	if (path)
		strbuf_addstr(&pathbuf, path);
	maxlinks = (flags & FOLLOW_SYMLINKS) ? MAX_SYMLINKS : -1;
	rc = walk_tree(sha1_root, &pathbuf, sha1_out, &tmp_mode, &tmp_errmsg,
		       maxlinks);
	strbuf_release(&pathbuf);
	if (mode)
		*mode = tmp_mode;
	if (errmsg)
		*errmsg = tmp_errmsg;
	else
		free(tmp_errmsg);
	return rc;
}
