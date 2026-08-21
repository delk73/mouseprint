#ifndef MOUSEPRINT_MOVEMENT_EPISODES_H
#define MOUSEPRINT_MOVEMENT_EPISODES_H

#include <cstdint>

struct sqlite3;

// Derives and replaces episodes for the completed runs in the database. The
// input evidence tables are read-only to this operation.
bool derive_movement_episodes(sqlite3* database);

#endif
