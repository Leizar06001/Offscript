# Offscript

Offscript is an AI-driven investigation game rendered as a 2.5D isometric
world in the terminal. Walk through a case, question its characters, compare
their stories, collect evidence, and corner the culprit.

Each character is backed by a live DeepSeek call built from authored facts,
personality, secrets, relationships, and memories of previous conversations.
The model performs the character, but the game engine remains responsible for
the world and the truth of the case.

Stories are ordinary JSON files. New investigations can be added without
recompiling the game.

## Features

- Streaming, in-character dialogue with visible emotions and physical actions
- Character-specific knowledge, lies, secrets, memories, and attitudes
- Multiple people in the same room, selectable with `Tab` or by name
- Spontaneous interjections and conversations between NPCs
- NPC movement within and between rooms, including following or joining others
- Authored clues and facts recorded in an investigation journal
- A free, engine-generated hint when the investigation stalls
- Multiple named saves for every story and automatic saving
- Random or author-selected culprits, with a generated resolution per game
- Configurable controls, ZQSD/WASD presets, reasoning level, and token prices
- Live API call, token, and estimated cost counters

The interface and bundled investigations are currently written in French.

## Bundled investigations

| Investigation | Setting | Characters | Rooms |
|---|---|---:|---:|
| La Fausse Princesse | Palais d'Orvain | 7 | 10 |
| Le Banquet des Cendres | Château de Valdoren | 6 | 11 |
| Le Sabotage d'Orion | Kepler lunar-orbit launch complex | 6 | 14 |
| Projet Chimère | Institut Hélianthe, Lyon | 6 | 11 |
| Projet Écho | Nexus Robotics underground laboratory, Paris | 5 | 8 |
| Protocole Mnésis V1 | Neo-Paris, Tour MNESIS | 6 | 11 |
| Protocole Mnésis V2 | Neo-Paris, Tour MNESIS | 6 | 11 |

## Requirements

- A Unix-like system (Linux or macOS)
- `gcc` or Apple Clang, plus `make`
- libcurl
- ncurses with wide-character support
- OpenSSL
- An internet connection and a [DeepSeek API key](https://platform.deepseek.com)

### Debian / Ubuntu

```sh
sudo apt update
sudo apt install build-essential libcurl4-openssl-dev libncurses-dev libssl-dev
```

### macOS

Install the command-line developer tools and Homebrew's OpenSSL 3 package:

```sh
xcode-select --install
brew install openssl@3
```

The Makefile detects Homebrew's `openssl@3` prefix automatically. On macOS it
links against the system `curl` and `ncurses`; on Linux it uses `ncursesw`.

## Build and run

```sh
make
./Offscript
```

To investigate prompt or game-mechanics problems, open **Options → Diagnostic**
and enable the JSONL journal. The setting persists across restarts. It writes a
timestamped file under `diagnostics/`. Each line records one
event: full model requests and responses, parsed dialogue, accepted or rejected
movement effects, memory-analysis results, relationship changes, discoveries,
and session-state snapshots. The directory and files are private to the current
user (`0700`/`0600`) and ignored by Git. They still contain complete
conversations, story solutions, character secrets, and prompts, so review a
file before sharing it. API keys are never recorded.

The same submenu has a separate setting for short in-game diagnostic alerts.
Those alerts only appear when something went wrong and do not require the JSONL
journal to be enabled.

Other build targets:

```sh
make clean    # remove object and dependency files
make fclean   # also remove the executable
make re       # rebuild everything
```

### First launch

Offscript asks for a DeepSeek API key, verifies it, and stores it locally as an
AES-256-GCM encrypted file. The encryption key is derived from the current
machine and user account, so copying the file to another account or computer
does not make it usable there.

By default, application settings live under:

```text
~/.config/enquete/credentials.enc
~/.config/enquete/options.json
```

If `XDG_CONFIG_HOME` is set, that directory is used instead of `~/.config`.
The API key can be replaced from the options menu.

After choosing an investigation, select an existing save or enter a name for a
new one. The story provides the player's title, so a player named `Poireau`
might become `Detective Poireau`. Saves are stored in the project at:

```text
saves/<story_id>/<player_name>.json
```

They are written after exchanges, every 20 seconds, and when quitting.

## Controls

Outside discussion mode, letter keys remain available for game actions. Press
`Enter` before typing a question.

| Default key | Action |
|---|---|
| Arrow keys or `ZQSD` | Move |
| `Enter` | Enter discussion mode or send the current question |
| `Enter` on an empty question | Leave discussion mode |
| Left/Right arrows in discussion mode | Move inside the question |
| `Tab` | Select another person in the room |
| `PgUp` / `PgDn` or `A` / `E` | Scroll through the conversation |
| `F3` | Open the investigation journal |
| `F2` | Change the player's appearance |
| `Esc` | Open the pause menu |

The pause menu provides the journal, a contextual hint, options, the solution,
and quit. Revealing the solution is treated as abandoning the investigation.

All actions can have two bindings and can be changed from the options menu,
available through `Esc` in game or `O` on the investigation list.

You can speak to anyone in the current room. The status line lists the people
present and underlines the current target. Naming a known character directly
in a question also selects them.

## Investigation model

Offscript separates authored truth from model-generated performance:

- The story file defines every fact, character, clue, room, and possible
  suspect. A character only receives facts listed in their own knowledge.
- The model proposes dialogue, emotion, physical action, movement, memories,
  relationship changes, clues produced, and—when justified—a confession.
- The engine validates identifiers, movement permissions, reachable paths,
  clues, and confessions. Invented fact or clue identifiers are discarded.
- When a character actually produces an authored clue, the clue and the facts
  it reveals are added to the player's journal.
- A confession only resolves the case when it comes from the selected culprit
  and the player has established enough incriminating facts.

NPCs may wander inside their assigned room. Characters with
`can_change_room: true` may also move to another room or join another NPC when
their response calls for it. Paths are calculated by the engine and avoid the
player and other characters.

The complete displayed conversation—including actions and system messages—is
kept in the save and restored when the game resumes. This is separate from the
small `recent_messages` window sent back to the model, so a long visible chat
does not enlarge prompts or their cost. Long-term memories, relationships,
discovered facts and clues, identities, positions, appearances, and movement
permissions are also persisted. See
[Memory_instructions.md](ressources/Memory_instructions.md) for the memory design.

## Models and cost settings

Two models are used:

- `DEEPSEEK_MODEL` handles dialogue and memory analysis. Its default is
  `deepseek-v4-flash`.
- `DEEPSEEK_MODEL_STORY` prepares a new game: it generates the resolution and,
  only when the authored evidence leaves a solvability gap, fills that gap with
  a clue and a second source. Its default is `deepseek-v4-pro`.

Override either model for one run:

```sh
DEEPSEEK_MODEL=your-model DEEPSEEK_MODEL_STORY=your-story-model ./Offscript
```

The options menu controls reasoning effort (`low`, `high`, or `max`) and the
input/output prices used by the on-screen estimate. The displayed cost is an
estimate, not a bill.

For movement diagnostics, run the game with:

```sh
OFFSCRIPT_DEBUG_MOVE=1 ./Offscript
```

## Adding a story

Copy the documented [story template](ressources/story_template.json):

```sh
mkdir -p ressources/my_story
cp ressources/story_template.json ressources/my_story/my_story_story.json
```

Any `*_story.json` file inside a subdirectory of `ressources/` is discovered
by the launch menu. The template contains `_lisez_moi` and `_aide` fields that
explain each section; the loader ignores those fields.

A story contains:

- metadata, premise, player title, and journal objectives;
- global AI directives and dialogue limits;
- the canonical facts of the case;
- characters, personalities, alibis, secrets, and individual knowledge;
- relationships, timeline entries, and discoverable clues;
- the map, room names, and character placement;
- the victim, suspects, crime, and solution settings.

### Keeping a case solvable

Every incriminating fact should have **two sources independent of the
culprit**. A source can be another character who knows the fact or an authored
clue that someone else can produce. Because the culprit may refuse to reveal
what incriminates them, making them the only source creates a circular and
potentially unwinnable case.

A fact that appears in neither a character's `known_fact_ids` nor a clue's
`reveals_fact_ids` can never be discovered.

The engine audits the incriminating facts selected for a new game. If one is
only obtainable from the culprit, the story model can create a matching clue
and assign a second holder. Those additions belong to that save and are
restored when the game is resumed. This is a safety net; well-authored stories
should still satisfy the two-source rule themselves.

### Maps and characters

The `map.rows` array is the tile grid:

- `1` and `2` are walls of different heights;
- `v` and `h` are doors;
- a space is walkable floor.

`player_start` defines the entrance. Each item in `map.rooms` gives a room an
identifier, display name, and one interior point. The engine flood-fills the
room from that point, so non-rectangular rooms require no special format.

Horizontal movement advances by two text columns. Keep reachable floor and
horizontal doors on the same column parity as `player_start`; the template
contains practical map-design warnings and a working example.

Place a character with a `game` block:

```json
"game": {
  "room": "laboratoire",
  "face": 0,
  "body": 0,
  "legs": 0,
  "fixed_face": false,
  "can_move": true,
  "can_change_room": true
}
```

The engine chooses a free position inside `room`. Explicit `x` and `y`
coordinates can be used when an exact position is required.

Set `mystery.solution.culprit_id` to a character ID for a fixed culprit, or to
`null` to draw one from `suspect_ids` for each new game. With a random culprit,
the story model writes and freezes a resolution consistent with the authored
facts before play begins.
