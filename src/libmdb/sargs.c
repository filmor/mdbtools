/* MDB Tools - A library for reading MS Access database file
 * Copyright (C) 2000 Brian Bruns
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "mdbtools.h"

#ifdef DMALLOC
#include "dmalloc.h"
#endif

void
mdb_sql_walk_tree(MdbSargNode *node, MdbSargTreeFunc func, gpointer data)
{
	if (func(node, data))
		return;
	if (node->left) mdb_sql_walk_tree(node->left, func, data);
	if (node->right) mdb_sql_walk_tree(node->right, func, data);
}
int 
mdb_test_string(MdbSargNode *node, char *s)
{
int rc;

	if (node->op == MDB_LIKE) {
		return mdb_like_cmp(s,node->value.s);
	}
	rc = strncmp(node->value.s, s, 255);
	switch (node->op) {
		case MDB_EQUAL:
			if (rc==0) return 1;
			break;
		case MDB_GT:
			if (rc<0) return 1;
			break;
		case MDB_LT:
			if (rc>0) return 1;
			break;
		case MDB_GTEQ:
			if (rc<=0) return 1;
			break;
		case MDB_LTEQ:
			if (rc>=0) return 1;
			break;
		default:
			fprintf(stderr, "Calling mdb_test_sarg on unknown operator.  Add code to mdb_test_string() for operator %d\n",node->op);
			break;
	}
	return 0;
}
int mdb_test_int(MdbSargNode *node, gint32 i)
{
	switch (node->op) {
		case MDB_EQUAL:
			if (node->value.i == i) return 1;
			break;
		case MDB_GT:
			if (node->value.i < i) return 1;
			break;
		case MDB_LT:
			if (node->value.i > i) return 1;
			break;
		case MDB_GTEQ:
			if (node->value.i <= i) return 1;
			break;
		case MDB_LTEQ:
			if (node->value.i >= i) return 1;
			break;
		default:
			fprintf(stderr, "Calling mdb_test_sarg on unknown operator.  Add code to mdb_test_int() for operator %d\n",node->op);
			break;
	}
	return 0;
}
#if 0
#endif
int
mdb_find_indexable_sargs(MdbSargNode *node, gpointer data)
{
	MdbSarg sarg;

	if (node->op == MDB_OR || node->op == MDB_NOT) return 1;

	/* 
	 * right now all we do is look for sargs that are anded together from
	 * the root.  Later we may put together OR ops into a range, and then 
	 * range scan the leaf pages. That is col1 = 2 or col1 = 4 becomes
	 * col1 >= 2 and col1 <= 4 for the purpose of index scans, and then
	 * extra rows are thrown out when the row is tested against the main
	 * sarg tree.  range scans are generally only a bit better than table
	 * scanning anyway.
	 *
	 * also, later we should support the NOT operator, but it's generally
	 * a pretty worthless test for indexes, ie NOT col1 = 3, we are 
	 * probably better off table scanning.
	 */
	if (mdb_is_relational_op(node->op)) {
		//printf("op = %d value = %s\n", node->op, node->value.s);
		sarg.op = node->op;
		sarg.value = node->value;
		mdb_add_sarg(node->col, &sarg);
	}
	return 0;
}
int 
mdb_test_sarg(MdbHandle *mdb, MdbColumn *col, MdbSargNode *node, void *buf, int len)
{
char tmpbuf[256];
int lastchar;

	switch (col->col_type) {
		case MDB_BYTE:
			return mdb_test_int(node, (int)((char *)buf)[0]);
			break;
		case MDB_INT:
			return mdb_test_int(node, mdb_get_int16(buf, 0));
			break;
		case MDB_LONGINT:
			return mdb_test_int(node, mdb_get_int32(buf, 0));
			break;
		case MDB_TEXT:
			if (IS_JET4(mdb)) {
				mdb_unicode2ascii(mdb, buf, 0, len, tmpbuf);
			} else {
				strncpy(tmpbuf, buf,255);
				lastchar = len > 255 ? 255 : len;
				tmpbuf[lastchar]='\0';
			}
			return mdb_test_string(node, tmpbuf);
		default:
			fprintf(stderr, "Calling mdb_test_sarg on unknown type.  Add code to mdb_test_sarg() for type %d\n",col->col_type);
			break;
	}
	return 1;
}
int
mdb_find_field(int col_num, MdbField *fields, int num_fields)
{
	int i;

	for (i=0;i<num_fields;i++) {
		if (fields[i].colnum == col_num) return i;
	}
	return -1;
}
int
mdb_test_sarg_node(MdbHandle *mdb, MdbSargNode *node, MdbField *fields, int num_fields)
{
	int elem;
	MdbColumn *col;
	int rc;

	if (mdb_is_relational_op(node->op)) {
		col = node->col;
		elem = mdb_find_field(col->col_num, fields, num_fields);
		if (!mdb_test_sarg(mdb, col, 
				node, 
				fields[elem].value, 
				fields[elem].siz)) 
			return 0;
	} else { /* logical op */
		switch (node->op) {
		case MDB_NOT:
			rc = mdb_test_sarg_node(mdb, node->left, fields, num_fields);
			return !rc;
			break;
		case MDB_AND:
			if (!mdb_test_sarg_node(mdb, node->left, fields, num_fields))
				return 0;
			return mdb_test_sarg_node(mdb, node->right, fields, num_fields);
			break;
		case MDB_OR:
			if (mdb_test_sarg_node(mdb, node->left, fields, num_fields))
				return 1;
			return mdb_test_sarg_node(mdb, node->right, fields, num_fields);
			break;
		}
	}
	return 1;
}
int 
mdb_test_sargs(MdbTableDef *table, MdbField *fields, int num_fields)
{
	MdbSargNode *node;
	MdbCatalogEntry *entry = table->entry;
	MdbHandle *mdb = entry->mdb;

	node = table->sarg_tree;

	/* there may not be a sarg tree */
	if (!node) return 1;

	return mdb_test_sarg_node(mdb, node, fields, num_fields);
}
#if 0
int mdb_test_sargs(MdbHandle *mdb, MdbColumn *col, int offset, int len)
{
MdbSarg *sarg;
int i;

	for (i=0;i<col->num_sargs;i++) {
		sarg = g_ptr_array_index (col->sargs, i);
		if (!mdb_test_sarg(mdb, col, sarg, offset, len)) {
			/* sarg didn't match, no sense going on */
			return 0;	
		}
	}

	return 1;
}
#endif
int mdb_add_sarg(MdbColumn *col, MdbSarg *in_sarg)
{
MdbSarg *sarg;
        if (!col->sargs) {
		col->sargs = g_ptr_array_new();
	}
	sarg = g_memdup(in_sarg,sizeof(MdbSarg));
        g_ptr_array_add(col->sargs, sarg);
	col->num_sargs++;

	return 1;
}
int mdb_add_sarg_by_name(MdbTableDef *table, char *colname, MdbSarg *in_sarg)
{
MdbColumn *col;
int i;

	for (i=0;i<table->num_cols;i++) {
		col = g_ptr_array_index (table->columns, i);
		if (!strcasecmp(col->name,colname)) {
			return mdb_add_sarg(col, in_sarg);
		}
	}
	/* else didn't find the column return 0! */
	return 0;
}
