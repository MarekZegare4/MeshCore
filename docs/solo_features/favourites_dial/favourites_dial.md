## Favourites Dial

[Go back](../../../README.md)

### Overview

|            OLED            |           E-Ink            |
| :------------------------: | :------------------------: |
| ![](./overview_oled.png) | ![](./overview_eink.png) |

A dedicated home page showing a grid of up to 6 pinned conversations — chat contacts, room servers or channels — for quick access. The layout adapts to the display orientation:

- **Portrait** (OLED, e-ink portrait) — 2 columns × 3 rows
- **Landscape** (e-ink landscape) — 3 columns × 2 rows

---

### Navigation

Navigate tiles with **UP / DOWN / LEFT / RIGHT**. Pressing a directional key at the edge of the grid switches to the adjacent home page instead of wrapping.

**Enter on a filled tile** — opens that conversation directly: a contact's DM, a channel's history, or a room server (running the room's login handshake first if it isn't logged in yet).

**Enter on an empty tile (`+`)** — starts the picker to fill the slot.

**Hold Enter on a filled tile** — opens **Unpin** / **Replace** for that slot.

Channel tiles show their name with a leading `#`, so they read apart from contacts and rooms sharing the same grid.

---

### Unread badge

Filled tiles show an unread message count in the top-right corner — unread DMs for a contact or room, unread posts for a channel. The name is ellipsized to make room for the badge.

If a pinned target disappears — a contact removed explicitly or auto-evicted to make room when the table is full, or a deleted channel — its slot is freed automatically and goes back to an empty `+` tile.

---

### Pinning

**From the Favourites Dial** — press **Enter** on an empty tile (`+`), or **Hold Enter** on a filled one and choose **Replace**. This opens the **Messages** screen in its normal Direct / Channels / Rooms browse; pick an entry and you land back on the dial with that slot filled. It's the same list you already use to open a conversation, rather than a second browser of its own.

|            OLED            |           E-Ink            |
| :------------------------: | :------------------------: |
| ![](./picker_oled.png) | ![](./picker_eink.png) |

<!-- screenshot pending: Messages browse entered from an empty dial tile -->

**From the Messages lists** — **Hold Enter** on a contact, room or channel entry › **Pin to dial**, then choose a slot from the slot picker (Slot 1–6, showing the current occupant or "empty").

**From Tools › Nodes** — **Hold Enter** on a node › **Pin to dial**. This one doesn't ask which slot; it takes the first free one (and says which), since that menu is already long.

If the target is already pinned in another slot, it is moved to the new slot automatically.

---

### Unpinning

**From the Favourites Dial** — **Hold Enter** on the tile › **Unpin**.

**From the Messages lists or Tools › Nodes** — **Hold Enter** › context menu › **Unpin (slot N)**.

---

### Pinning is not the same as favouriting

A pinned tile and a **Fav: ON** entry are two independent things. Pinning puts something on this page. Favouriting marks it with a ★ and sorts it to the top of every list it appears in, and is what the **Settings › Contacts** list filters read. Either can be used without the other.

---

### Reordering the Favourites page

The position of the Favourites Dial in the home page navigation sequence can be changed in **Settings › Home Pages** — press **LEFT / RIGHT** on the Favourites entry to move it earlier or later.
