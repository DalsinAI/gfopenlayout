/* GFOpenLayout's documents styled through GFCSS: what openlayout.c and
 * openlayout_css.c share. Not a public header.
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef OPENLAYOUT_CSS_H
#define OPENLAYOUT_CSS_H

#include "gfopenlayout.h"

/* openlayout_css.c */
void ol_css_node_free(void *css);
void ol_css_document_free(void *css);
/* styles the document from its sheets when they, its elements or the width changed; 0: out of memory */
int ol_css_restyle(ol_document *doc, ol_unit width);

/* openlayout.c: the tree */
void *ol_css_node_data(const ol_node *node);
void ol_css_set_node_data(ol_node *node, void *css);
void *ol_css_document_data(const ol_document *doc);
void ol_css_set_document_data(ol_document *doc, void *css);
ol_document *ol_css_node_document(const ol_node *node);
size_t ol_css_node_count(const ol_document *doc);
ol_node *ol_css_first_child(ol_node *node);
ol_node *ol_css_next_sibling(ol_node *node);
ol_node *ol_css_parent(ol_node *node);
void ol_css_move_child(ol_node *node, int first);

#endif
