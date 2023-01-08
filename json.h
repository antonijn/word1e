#ifndef JSON_H
#define JSON_H

#include <stdio.h>
#include <stdint.h>

#define JSON_MAX_LEVEL 32

typedef struct {
	FILE *output;

	/* each level has a bit indicating whether values have already
	 * been written (so that no trailing comma's are written in
	 * collections) */
	uint64_t level_bits;
	int level;

	enum {
		JSON_NO_ERROR,
		JSON_TOO_DEEP,
	} error;
} JSONWriter;

void json_writer_init(JSONWriter *j, FILE *f);
void json_writer_destroy(JSONWriter *j);

void json_double(JSONWriter *j, double d);
void json_int(JSONWriter *j, int i);
void json_string(JSONWriter *j, const char *s);
void json_null(JSONWriter *j);

void json_enter_assoc(JSONWriter *j, const char *key);
void json_leave_assoc(JSONWriter *j);
void json_enter_dict(JSONWriter *j);
void json_leave_dict(JSONWriter *j);
void json_enter_list(JSONWriter *j);
void json_leave_list(JSONWriter *j);

#endif
