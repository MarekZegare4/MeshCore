#pragma once
#include <Arduino.h>
#include <helpers/TransportKeyStore.h>
#include <helpers/TxtDataHelpers.h>

// Shared named-scope list: Settings > Radio > Scope used to be a single
// free-typed region name/key pair (NodePrefs.default_scope_name/
// default_scope_key). This replaces it with a small definable list, single-
// selected per channel (MessagesScreen's channel context menu) and multi-
// selected by the repeater's accept-filter (RepeaterScreen's Extra scopes,
// NodePrefs.repeat_extra_scope_mask) -- see MyMesh::rebuildRepeatScopes()
// and sendFloodScoped(const mesh::GroupChannel&, ...).
//
// List index 0 is the permanent, non-deletable, non-renamable "*" (wildcard/
// unscoped) -- it is never stored in entries[]/count below, only synthesised
// on read, so it can't be corrupted or migrated away. Real entries live at
// list index 1..count, i.e. entries[idx-1].
struct ScopeEntry {
  char    name[24];
  uint8_t key[16];
};

class ScopeList {
public:
  static const uint8_t MAX_SCOPE_ENTRIES = 8;   // named entries; list indices 1..MAX_SCOPE_ENTRIES

  uint8_t     count = 0;         // how many of entries[] are in use
  uint8_t     default_idx = 0;   // list index (0 = "*") used for DMs and any
                                  // channel/repeater slot without its own pick
  ScopeEntry  entries[MAX_SCOPE_ENTRIES];

  // Number of selectable list entries, "*" included.
  int totalCount() const { return count + 1; }

  bool isWildcard(uint8_t idx) const { return idx == 0; }

  // Clamp a possibly-stale index (e.g. read from an older/shorter list, or a
  // channel's saved pick after entries were deleted) to a valid one.
  uint8_t clamp(uint8_t idx) const { return (idx <= count) ? idx : 0; }

  const char* name(uint8_t idx) const {
    idx = clamp(idx);
    return (idx == 0) ? "*" : entries[idx - 1].name;
  }

  // "*" (and any out-of-range index) resolves to a null TransportKey, which
  // sendFloodScoped() already treats as "send unscoped" -- same behaviour a
  // never-configured device has today.
  TransportKey key(uint8_t idx) const {
    TransportKey k;
    idx = clamp(idx);
    if (idx == 0) memset(k.key, 0, sizeof(k.key));
    else          memcpy(k.key, entries[idx - 1].key, sizeof(k.key));
    return k;
  }

  // Derives a scope's key the same "#name" -> SHA256 way
  // MyMesh::setPrimaryScope() already does -- reused via TransportKeyStore
  // rather than re-implemented here.
  static void deriveKey(const char* name, uint8_t key[16]) {
    char hashtag[1 + 24];
    snprintf(hashtag, sizeof(hashtag), "#%s", name);
    TransportKeyStore temp;
    TransportKey tk;
    temp.getAutoKeyFor(0, hashtag, tk);
    memcpy(key, tk.key, 16);
  }

  // Adds a new named entry (deriving its key), returns its list index, or 0
  // if the list is full or the name is empty.
  uint8_t add(const char* name) {
    if (!name || !name[0] || count >= MAX_SCOPE_ENTRIES) return 0;
    ScopeEntry& e = entries[count];
    StrHelper::strncpy(e.name, name, sizeof(e.name));
    deriveKey(e.name, e.key);
    count++;
    return count;   // list index of the new entry
  }

  // Removes list index idx (1..count) -- index 0 ("*") can't be removed by
  // callers since it's never a valid argument here. Shifts later entries
  // down to close the gap and fixes up default_idx if it pointed at the
  // removed entry or anything after it.
  void remove(uint8_t idx) {
    if (idx < 1 || idx > count) return;
    for (uint8_t i = idx; i < count; i++) entries[i - 1] = entries[i];
    count--;
    if (default_idx == idx) default_idx = 0;
    else if (default_idx > idx) default_idx--;
  }
};
