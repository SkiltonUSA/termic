#ifndef TERMIC_WORKSPACE_H
#define TERMIC_WORKSPACE_H
/* File names accepted by editor and agent. No devices, parent traversal,
 * hidden state, shell metacharacters or arbitrary executable writes. */
int source_name(const char *name);
int save_text(const char *path, const char *text, unsigned long limit);
#endif
