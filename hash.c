/***************************************************************************
 SimpleMail - Copyright (C) 2000 Hynek Schlawack and Sebastian Bauer

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
***************************************************************************/

/**
 * @file
 *
 * A implementation of hash tables for strings and a single associated
 * data field.
 */

#include "hash.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "support_indep.h"

/**
 * Obtain the hash value of a given string.
 *
 * @param str the string from which to obtain the hash value.
 */
unsigned long sdbm(const unsigned char *str)
{
	unsigned long hash = 0;
	int c;

  while ((c = *str++))
		hash = c + (hash << 6) + (hash << 16) - hash;

	return hash;
}

/**
 * Initialize the given hash table with space for 2^bits entries.
 *
 * @param ht the hash table to initialize.
 * @param bits the number of bits used to identify a bucket.
 * @param filename defines the name of the file that is associated to this hash.
 *  If the file exists, the hash table is initialized with the contents of the
 *  file.
 * @return 1 on success, 0 otherwise.
 */
int hash_table_init(struct hash_table *ht, int bits, const char *filename)
{
	FILE *fh;
	int i;
	unsigned int size;
	unsigned int mem_size;
	struct hash_bucket *table;

	if (!bits) return 0;

	size = 2;
	for (i=1;i<bits;i++)
		size <<= 1;

	mem_size = size*sizeof(struct hash_bucket);
	if (!(table = (struct hash_bucket*)malloc(mem_size)))
		return 0;
	memset(table,0,mem_size);

	ht->bits = bits;
	ht->mask = size - 1;
	ht->size = size;
	ht->table = table;
	ht->filename = filename;

	if (filename)
	{
		if ((fh = fopen(filename,"r")))
		{
			char buf[512];
			fgets(buf,sizeof(buf),fh);
			if (!strncmp(buf,"SMHASH1",7))
			{
				fgets(buf,sizeof(buf),fh);
				ht->data = atoi(buf);

				while (fgets(buf,sizeof(buf),fh))
				{
					int len = strlen(buf);
					char *lc;
					unsigned int data;

					if (len && buf[len-1] == '\n') buf[len-1] = 0;
					if ((len>1) && buf[len-2] == '\r') buf[len-2]=0;

					data = strtoul(buf,&lc,10);
					if (lc)
					{
						if (isspace((unsigned char)*lc))
						{
							char *string;
							lc++;
							if ((string = mystrdup(lc)))
								hash_table_insert(ht, string, data);
						}
					}
				}
			}
			fclose(fh);
		}
	}

	return 1;
}

/**
 * Removes all entries from the hash table. The hash table can be used again
 * after this call, it is empty.
 *
 * @param ht the hash table to clear.
 */
void hash_table_clear(struct hash_table *ht)
{
	unsigned int i, size, mem_size;

	if (ht->table)
	{
		for (i=0;i<ht->size;i++)
		{
			struct hash_bucket *hb = &ht->table[i];
			if (hb->entry.string) free((void*)hb->entry.string);
			hb = hb->next;
			while (hb)
			{
				struct hash_bucket *thb = hb->next;
				free((void*)hb->entry.string);
				free(hb);
				hb = thb;
			}
		}
		ht->data = 0;

		size = ht->size;
		mem_size = size*sizeof(struct hash_bucket);
		memset(ht->table,0,mem_size);
	}
}

/**
 * Gives back all resources occupied by the given hash table (excluding the
 * memory directly pointed to ht). The hash table can be no longer used after
 * this call returned.
 *
 * @param ht the hash table to clean
 */
void hash_table_clean(struct hash_table *ht)
{
	unsigned int i;

	if (ht->table)
	{
		for (i=0;i<ht->size;i++)
		{
			struct hash_bucket *hb = &ht->table[i];
			if (hb->entry.string) free((void*)hb->entry.string);
			hb = hb->next;
			while (hb)
			{
				struct hash_bucket *thb = hb->next;
				free((void*)hb->entry.string);
				free(hb);
				hb = thb;
			}
		}
		free(ht->table);
		ht->table = NULL;
		ht->data = 0;
	}
}


/**
 * Insert a new entry into the hash table.
 *
 * @param ht
 * @param string
 * @param data
 * @return
 */
struct hash_entry *hash_table_insert(struct hash_table *ht, const char *string, unsigned int data)
{
	unsigned int index;
	struct hash_bucket *hb,*nhb;

	if (!string) return NULL;

	index = sdbm((const unsigned char*)string) & ht->mask;
	hb = &ht->table[index];
	if (!hb->entry.string)
	{
		hb->entry.string = string;
		hb->entry.data = data;
		return &hb->entry;
	}

	nhb = malloc(sizeof(struct hash_bucket));
	if (!nhb) return NULL;

	*nhb = *hb;
	hb->next = nhb;
	hb->entry.string = string;
	hb->entry.data = data;
	return &hb->entry;
}

/**
 * Lookup an entry in the hash table.
 *
 * @param ht the hash table in which to search.
 * @param string the string to lookup
 * @return the entry or NULL.
 */
struct hash_entry *hash_table_lookup(struct hash_table *ht, const char *string)
{
	unsigned int index;
	struct hash_bucket *hb;

	if (!string) return NULL;
	index = sdbm((const unsigned char*)string) & ht->mask;
	hb = &ht->table[index];

	if (!hb->entry.string) return NULL;

	while (hb)
	{
		if (!strcmp(hb->entry.string,string)) return &hb->entry;
		hb = hb->next;
	}

	return NULL;
}

/**
 * Callback which is called for every entry as a result of hash_table_store().
 *
 * @param entry points to the entry that shall be saved.
 * @param data is assumed to be of type FILE * here.
 */
static void hash_table_store_callback(struct hash_entry *entry, void *data)
{
	fprintf((FILE*)data,"%d %s\n",entry->data,entry->string);
}

/**
 * Presists the hash table. Works only, if filename was given at
 * hash_table_init().
 *
 * @param ht the hash table to store.
 */
void hash_table_store(struct hash_table *ht)
{
	FILE *fh;

	if (!ht->filename) return;

	if ((fh = fopen(ht->filename,"w")))
	{
		fputs("SMHASH1\n",fh);
		fprintf(fh,"%d\n",ht->data);
		hash_table_call_for_each_entry(ht,hash_table_store_callback, fh);
		fclose(fh);
	}
}

/**
 * For each entry, call the given function.
 *
 * @param ht the hash table to interate.
 * @param func the function that shall be called.
 * @param data the additional user data that is passed to the function.
 */
void hash_table_call_for_each_entry(struct hash_table *ht, void (*func)(struct hash_entry *entry, void *data), void *data)
{
	unsigned int i;
	if (!func) return;

	for (i=0;i<ht->size;i++)
	{
		struct hash_bucket *hb = &ht->table[i];
		while (hb)
		{
			if (hb->entry.string)
				func(&hb->entry,data);
			hb = hb->next;
		}
	}
}
