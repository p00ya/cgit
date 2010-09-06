#ifndef UI_COMMIT_H
#define UI_COMMIT_H

extern void cgit_init_commit(struct cgit_context *ctx, char *hex);
extern void cgit_print_commit(char *hex, const char *prefix);

#endif /* UI_COMMIT_H */
