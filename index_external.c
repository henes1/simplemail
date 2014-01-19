/**
 * index_external.c - an external string index implementation for SimpleMail.
 * Copyright (C) 2013  Sebastian Bauer
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file index_external.c
 *
 * This is a naive implementation of an external memory data structure that
 * can be used for full text search. It is basically a b tree that stores all
 * suffixes of each document that is inserted.
 *
 * It is very slow at the moment, in particular as the strings are read multiple
 * times from the external media.
 */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "lists.h"

#include "index.h"
#include "index_private.h"
#include "index_external.h"

/* Get va_vopy() with pre-C99 compilers */
#ifdef __SASC
#define va_copy(dest,src) ((dest) = (src))
#elif defined(__GNUC__) && (__GNUC__ < 3)
#include <varargs.h>
#endif

struct bnode_element
{
	int str_offset;
	int str_len;
	int did;
	int gchild; /* Child containing values that are greater */
};

struct bnode_header
{
	int leaf;
	int num_elements; /* Number of following bnode_elements */
	int lchild; /* Child containing keys/strings that are all (lexicographical) smaller */
};

typedef struct bnode_header bnode;

struct index_external
{
	struct index index;
	struct list document_list;

	int block_size;
	int max_elements_per_node;
	int number_of_blocks;

	FILE *string_file;
	FILE *index_file;

	/** Identifies the block for the root node. TODO: Make this persistent */
	int root_node;

	bnode *tmp;
	bnode *tmp2;
	bnode *tmp3;
};

/**
 * Create a new node for the given index/btree.
 *
 * @param idx
 * @return
 */
static bnode *bnode_create(struct index_external *idx)
{
	struct bnode_header *bnode;

	if (!(bnode = malloc(idx->block_size + sizeof(struct bnode_element))))
		return NULL;

	memset(bnode, 0, idx->block_size + sizeof(struct bnode_element));
	return bnode;
}

/**
 * Free the memory associated to the given bnode.
 *
 * @param idx
 * @param bnode
 */
static void bnode_free(struct index_external *idx, bnode *bnode)
{
	free(bnode);
}

/**
 * Return the i'th element of the given bnode.
 *
 * @param idx
 * @param bnode_header
 * @param elem
 * @return
 */
static struct bnode_element *bnode_get_ith_element_of_node(struct index_external *idx, bnode *bnode, int i)
{
	return &(((struct bnode_element*)(bnode+1))[i]);
}

/**
 * Writes the given block to the given address.
 *
 * @param idx
 * @param node
 * @param address
 * @return
 */
static int bnode_write_block(struct index_external *idx, const bnode *node, int address)
{
	unsigned int offset = address * idx->block_size;
	if (fseek(idx->index_file, offset, SEEK_SET))
		return 0;
	if (fwrite(node, 1, idx->block_size, idx->index_file) != idx->block_size)
		return 0;
	return 1;
}

/**
 * Adds the given block to the index and return the block number.
 *
 * @param idx
 * @param node
 * @return -1 for an error.
 */
static int bnode_add_block(struct index_external *idx, const bnode *node)
{
	int new_address = idx->number_of_blocks++;
	if (!bnode_write_block(idx, node, new_address))
		return -1;
	return new_address;
}

/**
 * Reads the given block for the given address to the given node.
 *
 * @param idx
 * @param node
 * @param address
 * @return
 */
static int bnode_read_block(struct index_external *idx, bnode *node, int address)
{
	size_t rc;
	unsigned int offset = address * idx->block_size;
	if (fseek(idx->index_file, offset, SEEK_SET))
		return 0;
	rc = fread(node, 1, idx->block_size, idx->index_file);
	return 1;
}

/**
 * Reads the entire string for the given element,
 *
 * @param idx
 * @param element
 * @return the string which must be freed with free() when no longer in use.
 */
static char *bnode_read_string(struct index_external *idx, struct bnode_element *element)
{
	char *str;

	fseek(idx->string_file, element->str_offset, SEEK_SET);
	if (!(str = malloc(element->str_len + 1)))
		return 0;
	if (fread(str, 1, element->str_len, idx->string_file) != element->str_len)
		return 0;
	str[element->str_len] = 0;
	return str;
}

/**
 * Compares contents represented by the given element with the given text. If str is the contents,
 * then the function places the return value of strcmp(str, text) into *out_cmp.
 *
 * @param idx
 * @param e
 * @param text
 * @param out_cmp
 * @return 0 on failure, 1 on success.
 */
static int bnode_compare_string(struct index_external *idx, struct bnode_element *e, const char *text, int *out_cmp)
{
	char *str = bnode_read_string(idx, e);
	if (!str) return 0;

	*out_cmp = strcmp(str, text);
	free(str);
	return 1;
}

#define BNODE_PATH_MAX_NODES 24

/**
 * Represents a path in the bnode tree.
 */
struct bnode_path
{
	int max_level;
	struct
	{
		int block;
		int key_index;
	} node[BNODE_PATH_MAX_NODES];
};

/**
 * Lookup the given text.
 *
 * @param idx
 * @param text
 * @param out_block_array
 * @param out_index
 * @return
 */
static int bnode_lookup(struct index_external *idx, const char *text, struct bnode_path *path)
{
	int i;
	bnode *tmp = idx->tmp;
	int block = idx->root_node;
	int level = 0;

	do
	{
		int lchild;
		int direct_match = 0;

		bnode_read_block(idx, tmp, block);

		path->max_level = level;
		path->node[level].block = block;

		/* Find the slot with the separation key for the given text. The slot with
		 * the separation key for a given text is the slot whose value is not
		 * lexicographically smaller than the text but whose left neighbor slot value
		 * is lexicographically smaller than the text.
		 *
		 * TODO: Use binary search
		 */
		for (i=0; i < tmp->num_elements; i++)
		{
			int cmp;
			struct bnode_element *e = bnode_get_ith_element_of_node(idx, tmp, i);
			if (!bnode_compare_string(idx, e, text, &cmp))
				return 0;

			/* if str > text then we ascend to lchild (except if we are at a leaf, of course) */
			if (cmp >= 0)
			{
				direct_match = cmp == 0;
				break;
			}
		}

		if (i == 0) lchild = tmp->lchild;
		else lchild = bnode_get_ith_element_of_node(idx, tmp, i - 1)->gchild;

		path->node[level].key_index = i;

		/* Leave early if this was a direct match with a separation key.
		 * In this case, we do not need to descend to the child */
		if (direct_match)
			goto out;

		if (block == lchild && !tmp->leaf)
		{
			fprintf(stderr, "Endless loop detected!\n");
			return 0;
		}

		/* Otherwise, it was no direct match, and the separation key is lexicographically strictly
		 * greater hence the sub tree defined by the left child, which is lexicographically strictly
		 * smaller. will contain the key (or a better suited value if the key is not contained).
		 */
		block = lchild;
		level++;
		if (level == BNODE_PATH_MAX_NODES)
			return 0;
	} while (!tmp->leaf);

out:
	return 1;
}

/**
 * Clear all elements of the given node begining at start.
 *
 * @param idx
 * @param n
 * @param start
 */
static void bnode_clear_elements(struct index_external *idx, bnode *n, int start)
{
	struct bnode_element *e = bnode_get_ith_element_of_node(idx, n, start);
	memset(e, (idx->max_elements_per_node - start)*sizeof(struct bnode_element), 0);
}

/**
 * Dump the children of the given nodes.
 */
static void dump_node_children(struct index_external *idx, bnode *node, const char *prefix)
{
	int i;
	printf("%s: ", prefix, node->lchild);
	for (i=0;i<node->num_elements;i++)
	{
		printf("%d ",bnode_get_ith_element_of_node(idx, node, i)->gchild);
	}
	printf("\n");
}

/**
 * Dump the entire index.
 *
 * @param idx
 * @param block
 * @param level
 */
static void dump_index(struct index_external *idx, int block, int level)
{
	int i;
	bnode *tmp = bnode_create(idx);
	if (!bnode_read_block(idx, tmp, block))
		return;

	printf("dump_index(block=%d, level=%d, leaf=%d) elements=%d\n", block, level, tmp->leaf, tmp->num_elements);
	if (!tmp->leaf)
		dump_index(idx, tmp->lchild, level + 1);

	for (i=0; i<tmp->num_elements; i++)
	{
		char *str;
		struct bnode_element *e;
		char buf[16];

		e = bnode_get_ith_element_of_node(idx, tmp, i);
		if (!(str = bnode_read_string(idx, e)))
		{
			fprintf(stderr, "Couldn't read string!\n");
			exit(-1);
		}
		snprintf(buf,sizeof(buf),"%s", str);

		{
			int k;
			for (k=0;k<level;k++)
				printf(" ");
		}
		printf("l%d: b%d: i%d: o%04d ", level, block, i, e->str_offset);

		{
			int k;
			for (k=0;k<strlen(buf);k++)
			{
				if (!buf[k]) break;
				if (buf[k] == 10) buf[k] = ' ';
				printf("%c", buf[k]);
			}
			printf("\n");
		}
		if (!tmp->leaf)
			dump_index(idx, e->gchild, level + 1);

		free(str);
	}

	bnode_free(idx, tmp);
}

/**
 * Verify whether the index is consitent, i.e., whether all strings are in increasing order.
 *
 * @param
 * @param block
 * @param level
 */
static void verify_index(struct index_external *idx, int block, int level)
{
	int i;
	bnode *tmp = bnode_create(idx);
	char *str1 = strdup("");
	char *str2;
	int offset1 = -1;

	if (!bnode_read_block(idx, tmp, block))
		return;

	if (!tmp->leaf)
		verify_index(idx, tmp->lchild, level + 1);

	for (i=0; i<tmp->num_elements; i++)
	{
		struct bnode_element *e;
		int rc;

		e = bnode_get_ith_element_of_node(idx, tmp, i);
		str2 = bnode_read_string(idx, e);
		if ((rc = strcmp(str1,str2)) > 0)
		{
			int *a = 0;
			printf("violation\n");
			printf("%d: %d: %d: %d-%d=%d |%s |%s (%d %d)\n", level, block, i, str1[0], str2[0], rc, str1, str2, offset1, e->str_offset);
			*a = 0;
			exit(1);
		}


		if (!tmp->leaf)
			verify_index(idx, e->gchild, level + 1);

		free(str1);
		str1 = str2;
		offset1= e->str_offset;
	}

	free(str1);
	bnode_free(idx, tmp);
}


/**
 * Verify whether the index is consitent, i.e., whether all strings are in increasing order.
 *
 * @param
 * @param block
 * @param level
 */
static int count_index(struct index_external *idx, int block, int level)
{
	int i, count = 0;
	bnode *tmp = bnode_create(idx);

	if (!bnode_read_block(idx, tmp, block))
		return;

	if (!tmp->leaf)
		count += count_index(idx, tmp->lchild, level + 1);

	for (i=0; i<tmp->num_elements; i++)
	{
		struct bnode_element *e;
		int rc;

		e = bnode_get_ith_element_of_node(idx, tmp, i);
		if (!tmp->leaf)
			count += count_index(idx, e->gchild, level + 1);
		count++;
	}

	bnode_free(idx, tmp);
	return count;
}

/**
 * Inserts the given string into the bnode tree.
 *
 * @param idx
 * @param did
 * @param txt
 * @return
 */
static int bnode_insert_string(struct index_external *idx, int did, int offset, const char *text)
{
	int i;
	int current_level;
	int block;
	bnode *tmp = idx->tmp;
	struct bnode_path path;
	struct bnode_element new_element;

	if (!bnode_lookup(idx, text, &path))
		return 0;

	current_level = path.max_level;
	new_element.str_offset = offset;
	new_element.str_len = strlen(text);
	new_element.did = did;
	new_element.gchild = 0;

	while (current_level >= 0)
	{
		struct bnode_element *e;

		block = path.node[current_level].block;
		i = path.node[current_level].key_index;

		bnode_read_block(idx, tmp, block);

		if (!tmp->leaf && current_level == path.max_level)
		{
			fprintf(stderr, "Cannot insert into a non-leaf\n");
			exit(1);
		}

		/* Recall that we allocate in our temps one more entry than it would fit in the block, so we surly can
		 * insert the entry now */
		e = bnode_get_ith_element_of_node(idx, tmp, i);

		memmove(e + 1, e, (tmp->num_elements - i) * sizeof(struct bnode_element));

		/* New element */
		*e = new_element;
		tmp->num_elements++;

		if (tmp->num_elements == idx->max_elements_per_node)
		{
			/* Now we split the node into two nodes. We keep the median out as we
			 * insert it as a separation value for the two nodes on the parent.
			 */

			int median = tmp->num_elements / 2;
			struct bnode_element *me = bnode_get_ith_element_of_node(idx, tmp, median);
			struct bnode_element me_copy = *me;

			/* First node */
			tmp->num_elements = median;

			/* Second node */
			idx->tmp3->num_elements = idx->max_elements_per_node - (median + 1);
			idx->tmp3->lchild = me->gchild;
			idx->tmp3->leaf = tmp->leaf;
			memcpy(bnode_get_ith_element_of_node(idx, idx->tmp3, 0), bnode_get_ith_element_of_node(idx, tmp, median + 1), idx->tmp3->num_elements * sizeof(struct bnode_element));
			bnode_clear_elements(idx, idx->tmp3, idx->tmp3->num_elements);
			int tmp3block = bnode_add_block(idx, idx->tmp3);

			/* This should be done as late as possible */
			bnode_clear_elements(idx, tmp, median);
			bnode_write_block(idx, tmp, block);

			if (block == idx->root_node)
			{
				assert(current_level == 0);

				/* Create a new root block if the root was getting full */
				tmp->num_elements = 1;
				tmp->lchild = idx->root_node;
				tmp->leaf = 0;
				me_copy.gchild = tmp3block;
				*bnode_get_ith_element_of_node(idx, tmp, 0) = me_copy;
				bnode_clear_elements(idx, tmp, 1);
				idx->root_node = bnode_add_block(idx, tmp);
				break;
			}

			new_element = me_copy;
			new_element.gchild = tmp3block;
		} else
		{
			/* There was enough space in the block */
			bnode_write_block(idx, tmp, block);
			break;
		}
		current_level--;
	}
	return 1;
}

/**
 * Tries to find the given string.
 *
 * @param idx
 * @param text
 * @return
 */
static int bnode_find_string(struct index_external *idx, const char *text, int (*callback)(int did, void *userdata), void *userdata)
{
	int i;
	int block;
	char *str;
	bnode *tmp = idx->tmp;
	struct bnode_element *be;
	int text_len = strlen(text);
	int nd = 0;
	struct bnode_path path;

	if (!bnode_lookup(idx, text, &path))
		return 0;

	block = path.node[path.max_level].block;
	i = path.node[path.max_level].key_index;

	if (!bnode_read_block(idx, tmp, block))
		return 0;

	for (;i<tmp->num_elements;i++)
	{
		int cmp;
		be = bnode_get_ith_element_of_node(idx, tmp, i);
		str = bnode_read_string(idx, be);
		if (!str) return 0;
		cmp = strncmp(text, str, strlen(text));
		free(str);
		if (!cmp)
		{
			callback(be->did, userdata);
			nd++;
		}
		else
			break;
	}
	return nd;
}

void index_external_dispose(struct index *index)
{
	struct document_node *d;
	struct index_external *idx;

	idx = (struct index_external*)index;

	if (idx->string_file) fclose(idx->string_file);
	if (idx->index_file) fclose(idx->index_file);
	if (idx->tmp3) bnode_free(idx, idx->tmp3);
	if (idx->tmp2) bnode_free(idx, idx->tmp2);
	if (idx->tmp) bnode_free(idx, idx->tmp);
	free(idx);
}

struct index *index_external_create(const char *filename)
{
	struct index_external *idx;
	char buf[380];

	if (!(idx = (struct index_external*)malloc(sizeof(*idx))))
		return NULL;

	memset(idx,0,sizeof(*idx));
	list_init(&idx->document_list);
	idx->block_size = 16384;
	idx->max_elements_per_node = (idx->block_size - sizeof(struct bnode_header)) / sizeof(struct bnode_element);

	if (!(idx->tmp = bnode_create(idx)))
		goto bailout;

	if (!(idx->tmp2 = bnode_create(idx)))
		goto bailout;

	if (!(idx->tmp3 = bnode_create(idx)))
		goto bailout;

	sm_snprintf(buf, sizeof(buf), "%s.index", filename);
	if (!(idx->index_file = fopen(buf, "w+b")))
		goto bailout;

	sm_snprintf(buf, sizeof(buf), "%s.strings", filename);
	if (!(idx->string_file = fopen(buf, "w+b")))
		goto bailout;

	idx->tmp->leaf = 1;
	idx->root_node = bnode_add_block(idx, idx->tmp);

	return &idx->index;
bailout:
	index_external_dispose(&idx->index);
	return NULL;
}

int index_external_put_document(struct index *index, int did, const char *text)
{
	struct index_external *idx;
	int i;
	int l = strlen(text);

	idx = (struct index_external*)index;

	/* Determine position and write text */
	if (fseek(idx->string_file, 0, SEEK_END) != 0)
		return 0;
	long offset = ftell(idx->string_file);
	fputs(text, idx->string_file);

	for (i=0; i < l; i++)
	{
		if (!(bnode_insert_string((struct index_external*)index, did, offset + i, text + i)))
			return 0;
	}

	return 1;
}

int index_external_remove_document(struct index *index, int did)
{
	return 0;
}

int index_external_find_documents(struct index *index, int (*callback)(int did, void *userdata), void *userdata, int num_substrings, va_list substrings)
{
	int i;
	int nd = 0;
	struct index_external *idx;
	va_list substrings_copy;

	idx = (struct index_external*)index;

	if (num_substrings != 1)
	{
		fprintf(stderr, "Searching with only one sub string is supported for now.\n");
		exit(-1);
	}

	va_copy(substrings_copy,substrings);

	for (i=0;i<num_substrings;i++)
	{
		nd = bnode_find_string(idx, va_arg(substrings_copy, char *), callback, userdata);
	}

	va_end(substrings_copy);

	return nd;
}

/*****************************************************/

struct index_algorithm index_external =
{
		index_external_create,
		index_external_dispose,
		index_external_put_document,
		index_external_remove_document,
		index_external_find_documents
};