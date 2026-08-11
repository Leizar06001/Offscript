# Offscript

An AI driven game based on predetermined stories and AI dialogues.

A 2.5D isometric world in the terminal. You play an investigator: walk up to a
character, type a question, and they answer in their own voice — because each
one is a real LLM call built from their personality, what they know, and what
they are hiding. Their expression changes on the map as they react, and they
remember what you told them from one conversation to the next.

Stories are plain JSON files, so adding a new case needs no recompilation.
The bundled story, *Projet Écho*, is in French.

## Requirements

- `gcc`, `make`
- libcurl, ncurses (wide), OpenSSL
- a [DeepSeek](https://platform.deepseek.com) API key

```sh
sudo apt install build-essential libcurl4-openssl-dev libncurses-dev libssl-dev
```

## Build and run

```sh
make
./MysteryBox
```

On first launch the game asks for your API key, checks it against the API, and
stores it encrypted (AES-256-GCM) in `~/.config/enquete/credentials.enc`, tied
to your machine and user account. It is never written to the source tree.
To use a different key, delete that file and relaunch.

Then you pick a case, and either resume one of your investigations or start a
new one under a new name. The story supplies the title, so entering *Poireau*
makes you *Detective Poireau*, and your save is filed under your own name —
several people can each have their own game of the same case.

## Controls

Outside chat mode the keyboard is free for actions: typing does not write into
your question until you take the floor.

| Key | |
|---|---|
| Arrows or `ZQSD` | Move |
| `Enter` | Take the floor (chat mode); again on an empty line to leave it |
| Arrows in chat mode | Move the cursor inside your question |
| `Tab` | Switch who you are addressing when several people share the room |
| `PgUp` / `PgDn` (or `A` / `E`) | Scroll back through the conversation |
| `F3` | Journal: the case, your objectives, one page per character |
| `F2` | Change your appearance |
| `ESC` | Menu: journal, options, solution, quit |

Every key above is rebindable in the options (`ESC` in game, or `O` from the
case list). Options are stored in `~/.config/enquete/options.json`, separate
from your saves, and hold a `WASD` preset as well as the token prices used by
the cost estimate.

You speak to whoever is in the room with you — the line above the map names
the room, who is there, and underlines the person who will answer. Naming
someone in your question also addresses them.

## Adding a story

Copy `ressources/story_template.json` to
`ressources/<your_story>/<your_story>_story.json`. Any folder there with a
`*_story.json` file shows up in the launch menu.

A story holds the facts of the case (each with a truth flag), the characters —
personality, alibi, secrets, and which facts they know — plus the timeline,
clues and the mystery itself.

The building belongs to the story too, in a `map` block: `rows` is the tile
grid (`2`/`1` walls, `v`/`h` doors, space for floor), `player_start` says where
you walk in, and `rooms` names the places. A room is described by one point
*inside* it, not by a rectangle — the engine floods the area from there, so an
L-shaped room needs no special handling. Doors are found automatically and
linked to the two rooms they separate, which is what lets characters walk from
one to another.

Give each character a `game` block to place them: `"room": "labo_b"` drops them
somewhere free in that room (`"x"`/`"y"` still work if you need an exact spot).
`"can_move"` and `"can_change_room"` say whether they may wander and whether
they may leave; the game can change both later, so a deactivated android can be
switched on mid-case. Add `"fixed_face": true` for a character whose face should
never follow their emotion (an android has only one plausible emoji).

`story.player_title` is how the case addresses the investigator ("Detective",
"Commissaire"…).

Leave `mystery.solution.culprit_id` as `null` and the engine draws a culprit at
each new game, then generates a resolution consistent with your facts — so the
case still adds up, and the guilty one lies coherently instead of improvising.

## How it works

- **The story file is the author's truth.** Characters only ever receive the
  facts they are supposed to know.
- **The save holds what changed**: per-character memory, relationship, revealed
  secrets, plus where everyone stands. It lives in
  `saves/<story_id>/<your_name>.json`, written after each exchange, every 20
  seconds, and on quit. See `ressources/Memory_instructions.md`.
- **The engine owns the truth.** The model only proposes a reply, a memory, a
  relationship nudge and at most a step across the room; the engine validates
  everything, and a fact id it invents is discarded.
- **The case ends when the culprit breaks.** The engine counts how many of the
  incriminating facts you have actually established; only past that point is
  the culprit allowed to confess, and only their confession ends the game.
  Accusing at random gets you denied.

Two models are used: a fast one for every reply and every memory pass, and a
stronger one for the single call that writes the case's resolution. Override
with `DEEPSEEK_MODEL` (default `deepseek-v4-flash`) and `DEEPSEEK_MODEL_STORY`
(default `deepseek-v4-pro`). The bottom line of the screen tracks how many
calls and tokens the case has cost, with a rough price estimate — the rates are
set in the options, so you can correct them when the provider's grid drifts.
