/*
 * Simple JSON marshalling tools.
 * Copyright (C) 2023  Antonie Blom
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "json.h"
#include <stdbool.h>
#include <stdint.h>

static void
separate(JSONWriter *j)
{
	uint64_t bit = (uint64_t)1 << j->level;
	if (j->level_bits & bit)
		fputc(',', j->output);
	else
		j->level_bits |= bit;
}

void json_writer_init(JSONWriter *j, FILE *f)
{
	j->output = f;
	j->level = 0;
	j->level_bits = 0;
	j->error = 0;
}
void json_writer_destroy(JSONWriter *j)
{
}

void json_double(JSONWriter *j, double d)
{
	separate(j);
	fprintf(j->output, "%f", d);
}

void json_int(JSONWriter *j, int i)
{
	separate(j);
	fprintf(j->output, "%d", i);
}

void
json_string(JSONWriter *j, const char *s)
{
	separate(j);

	/* TODO sanitize... */
	fprintf(j->output, "\"%s\"", s);
}

void json_null(JSONWriter *j)
{
	separate(j);

	fprintf(j->output, "null");
}

static void
json_enter(JSONWriter *j, const char *s)
{
	if (j->level >= JSON_MAX_LEVEL) {
		j->error = JSON_TOO_DEEP;
		return;
	}

	++j->level;
	j->level_bits &= ~((uint64_t)1 << j->level);

	fputs(s, j->output);
}

static void
json_leave(JSONWriter *j, const char *s)
{
	if (j->level <= 0) {
		j->error = JSON_TOO_DEEP;
		return;
	}

	--j->level;
	fputs(s, j->output);
}

void
json_enter_assoc(JSONWriter *j, const char *key)
{
	json_string(j, key);
	json_enter(j, ":");
}
void
json_leave_assoc(JSONWriter *j)
{
	json_leave(j, "");
}


void json_enter_dict(JSONWriter *j)
{
	separate(j);
	json_enter(j, "{");
}
void json_leave_dict(JSONWriter *j)
{
	json_leave(j, "}");
}

void json_enter_list(JSONWriter *j)
{
	separate(j);
	json_enter(j, "[");
}
void json_leave_list(JSONWriter *j)
{
	json_leave(j, "]");
}
