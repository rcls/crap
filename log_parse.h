#ifndef LOG_PARSE_H
#define LOG_PARSE_H

struct database;
struct cvs_connection;

/// Populate @c database from the given file @c f.  @c l and @c l_len are used
/// for storing lines as they are read fromthe file.
void read_files_versions (struct database * database,
                          struct cvs_connection * s);

#endif
