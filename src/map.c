
#include "includes.h"

/* Carte de secours, utilisee uniquement par une histoire sans bloc "map".
 * Les histoires livrees decrivent leur propre batiment (voir story.h). */
static const int fallback_w = 62;
static const int fallback_h = 22;

static const char fallback_map[] =
"22222222222222222222222222222222222222222222222222222222222222"
"2                         2        2                         2"
"2                         2        2                         2"
"2                         2        v                         2"
"2                         v        2                         2"
"2                         2        222222222222222222222222222"
"2                         2        2        2                2"
"222222222222222222222222222        2        2                2"
"2                         2        v        2                2"
"2                         2        2222222222                2"
"2                         2        v                         2"
"2                         v        2                         2"
"2                         2        2                         2"
"2                         2        2                         2"
"222222222222222222222222222        222222222222222222222222222"
"2                         2        v                         2"
"2                         2        2                         2"
"2                         2        2                         2"
"2                         v        2                         2"
"2                         2        2                         2"
"2                         2        2                         2"
"22222222222222222222222222222222222222222222222222222222222222";

/* ------------------------------------------------------------------ */
/* Lecture des cases                                                   */
/* ------------------------------------------------------------------ */

char map_tile(const Map *m, int x, int y) {
	if (!m || !m->map) return '\0';
	if (x < 0 || y < 0 || x >= m->w || y >= m->h) return '\0';
	return m->map[y * m->w + x];
}

int map_is_door_char(char c) {
	return c == 'v' || c == 'h';
}

/* Hors carte compte comme un mur : rien ne doit pouvoir en sortir. */
int map_is_wall(const Map *m, int x, int y) {
	char c = map_tile(m, x, y);
	if (c == '\0') return 1;
	if (c == m->map_c_empty || map_is_door_char(c)) return 0;
	return 1;
}

int map_can_stand(const Map *m, int x, int y) {
	return !map_is_wall(m, x, y);
}

/* Un pas horizontal couvre deux colonnes (la carte est dessinee en
 * isometrique) : la colonne enjambee doit etre libre elle aussi, sinon on
 * traverserait un mur d'une case d'epaisseur. */
int map_can_step(const Map *m, int x, int y, int dx, int dy) {
	int nx = x + dx, ny = y + dy;
	if (!map_can_stand(m, nx, ny)) return 0;
	if (dx == -2 && map_is_wall(m, x - 1, y)) return 0;
	if (dx ==  2 && map_is_wall(m, x + 1, y)) return 0;
	return 1;
}

/* ------------------------------------------------------------------ */
/* Pieces : remplissage depuis un point interieur                      */
/* ------------------------------------------------------------------ */

/* Remplit la zone de sol contenant (sx,sy) et l'attribue a `room`. Les portes
 * arretent le remplissage : sans cela toutes les pieces communiqueraient et
 * n'en formeraient qu'une seule. */
static void flood_room(Map *m, int room, int sx, int sy) {
	if (map_tile(m, sx, sy) != m->map_c_empty) return;
	if (m->room_of[sy * m->w + sx] != ROOM_NONE) return;

	int *stack = malloc(sizeof(int) * (size_t)m->w * (size_t)m->h);
	int top = 0;
	stack[top++] = sy * m->w + sx;
	m->room_of[sy * m->w + sx] = room;

	Room *r = &m->rooms[room];
	r->min_x = r->max_x = sx;
	r->min_y = r->max_y = sy;
	r->nb_tiles = 0;

	const int dx[4] = { 1, -1, 0, 0 };
	const int dy[4] = { 0, 0, 1, -1 };

	while (top > 0) {
		int idx = stack[--top];
		int x = idx % m->w, y = idx / m->w;
		r->nb_tiles++;
		if (x < r->min_x) r->min_x = x;
		if (x > r->max_x) r->max_x = x;
		if (y < r->min_y) r->min_y = y;
		if (y > r->max_y) r->max_y = y;

		for (int i = 0; i < 4; i++) {
			int nx = x + dx[i], ny = y + dy[i];
			if (map_tile(m, nx, ny) != m->map_c_empty) continue;
			if (m->room_of[ny * m->w + nx] != ROOM_NONE) continue;
			m->room_of[ny * m->w + nx] = room;
			stack[top++] = ny * m->w + nx;
		}
	}
	free(stack);
}

/* Chaque porte relie les deux pieces qu'elle separe. On regarde de part et
 * d'autre selon son orientation, en sautant les cases de porte pour gerer un
 * sas de deux portes accolees. */
static int room_beyond(const Map *m, int x, int y, int dx, int dy) {
	for (int step = 1; step <= 3; step++) {
		int nx = x + dx * step, ny = y + dy * step;
		char c = map_tile(m, nx, ny);
		if (c == '\0') return ROOM_NONE;
		if (map_is_door_char(c)) continue;
		if (c != m->map_c_empty) return ROOM_NONE;
		return m->room_of[ny * m->w + nx];
	}
	return ROOM_NONE;
}

static void detect_doors(Map *m) {
	m->nb_doors = 0;
	for (int y = 0; y < m->h && m->nb_doors < MAP_MAX_DOORS; y++) {
		for (int x = 0; x < m->w && m->nb_doors < MAP_MAX_DOORS; x++) {
			char c = map_tile(m, x, y);
			if (!map_is_door_char(c)) continue;

			Door *d = &m->doors[m->nb_doors++];
			d->x = x;
			d->y = y;
			d->vertical = (c == 'v');
			if (d->vertical) {
				d->room_a = room_beyond(m, x, y, -1, 0);
				d->room_b = room_beyond(m, x, y,  1, 0);
			} else {
				d->room_a = room_beyond(m, x, y, 0, -1);
				d->room_b = room_beyond(m, x, y, 0,  1);
			}
		}
	}
}

int map_room_at(const Map *m, int x, int y) {
	if (!m || !m->room_of) return ROOM_NONE;
	if (x < 0 || y < 0 || x >= m->w || y >= m->h) return ROOM_NONE;

	int room = m->room_of[y * m->w + x];
	if (room != ROOM_NONE) return room;

	/* Sur le seuil d'une porte : on rend la piece voisine, pour qu'un
	 * personnage arrete dans l'embrasure soit toujours quelque part. */
	if (map_is_door_char(map_tile(m, x, y))) {
		const int dx[4] = { -1, 1, 0, 0 };
		const int dy[4] = { 0, 0, -1, 1 };
		for (int i = 0; i < 4; i++) {
			int r = room_beyond(m, x, y, dx[i], dy[i]);
			if (r != ROOM_NONE) return r;
		}
	}
	return ROOM_NONE;
}

const char *map_room_name(const Map *m, int room) {
	if (!m || room < 0 || room >= m->nb_rooms) return NULL;
	return m->rooms[room].name;
}

int map_room_by_id(const Map *m, const char *id) {
	if (!m || !id) return ROOM_NONE;
	for (int i = 0; i < m->nb_rooms; i++) {
		if (strcmp(m->rooms[i].id, id) == 0) return i;
	}
	return ROOM_NONE;
}

/* ------------------------------------------------------------------ */
/* Placement                                                           */
/* ------------------------------------------------------------------ */

int map_spot_in_room(const Map *m, int room,
                     const int *busy_x, const int *busy_y, int nb_busy,
                     int *out_x, int *out_y) {
	if (!m || room < 0 || room >= m->nb_rooms) return -1;

	const Room *r = &m->rooms[room];

	/* On part du centre et on s'en ecarte : les personnages se repartissent
	 * dans la piece au lieu de s'entasser dans un coin. */
	int cx = (r->min_x + r->max_x) / 2;
	int cy = (r->min_y + r->max_y) / 2;
	int best_x = -1, best_y = -1, best_d = 0;

	for (int y = r->min_y; y <= r->max_y; y++) {
		for (int x = r->min_x; x <= r->max_x; x++) {
			if (map_room_at(m, x, y) != room) continue;
			if ((x & 1) != m->x_parity) continue;

			bool taken = false;
			for (int i = 0; i < nb_busy; i++) {
				if (busy_x[i] == x && busy_y[i] == y) { taken = true; break; }
			}
			if (taken) continue;

			int d = abs(x - cx) + abs(y - cy) * 2;
			if (best_x < 0 || d < best_d) { best_x = x; best_y = y; best_d = d; }
		}
	}

	if (best_x < 0) return -1;
	*out_x = best_x;
	*out_y = best_y;
	return 0;
}

int map_random_spot_in_room(const Map *m, int room, int pick, int *out_x, int *out_y) {
	if (!m || room < 0 || room >= m->nb_rooms) return -1;
	const Room *r = &m->rooms[room];

	/* Premier passage pour compter, second pour prendre la n-ieme : la piece
	 * n'est pas forcement rectangulaire, on ne peut pas calculer l'indice. */
	int count = 0;
	for (int y = r->min_y; y <= r->max_y; y++) {
		for (int x = r->min_x; x <= r->max_x; x++) {
			if (map_room_at(m, x, y) != room) continue;
			if ((x & 1) != m->x_parity) continue;
			count++;
		}
	}
	if (count <= 0) return -1;

	int want = ((pick % count) + count) % count;
	for (int y = r->min_y; y <= r->max_y; y++) {
		for (int x = r->min_x; x <= r->max_x; x++) {
			if (map_room_at(m, x, y) != room) continue;
			if ((x & 1) != m->x_parity) continue;
			if (want-- == 0) { *out_x = x; *out_y = y; return 0; }
		}
	}
	return -1;
}

/* ------------------------------------------------------------------ */
/* Trajets                                                             */
/* ------------------------------------------------------------------ */

/* Parcours en largeur sur les regles de deplacement reelles (pas de deux
 * colonnes a l'horizontale). Renvoie la case par laquelle on est arrive a
 * chaque position, ce qui permet de remonter le chemin. */
static int *bfs_from(const Map *m, int sx, int sy) {
	if (!map_can_stand(m, sx, sy)) return NULL;

	int n = m->w * m->h;
	int *prev = malloc(sizeof(int) * (size_t)n);
	int *queue = malloc(sizeof(int) * (size_t)n);
	for (int i = 0; i < n; i++) prev[i] = -2;   /* -2 = jamais atteint */

	int head = 0, tail = 0;
	queue[tail++] = sy * m->w + sx;
	prev[sy * m->w + sx] = -1;                  /* -1 = depart */

	const int dx[4] = { 0, 0, -2, 2 };
	const int dy[4] = { -1, 1, 0, 0 };

	while (head < tail) {
		int idx = queue[head++];
		int x = idx % m->w, y = idx / m->w;
		for (int i = 0; i < 4; i++) {
			if (!map_can_step(m, x, y, dx[i], dy[i])) continue;
			int nx = x + dx[i], ny = y + dy[i];
			int nidx = ny * m->w + nx;
			if (prev[nidx] != -2) continue;
			prev[nidx] = idx;
			queue[tail++] = nidx;
		}
	}
	free(queue);
	return prev;
}

static bool occupied_at(int x, int y,
                        const int *busy_x, const int *busy_y, int nb_busy) {
	for (int i = 0; i < nb_busy; i++)
		if (busy_x[i] == x && busy_y[i] == y) return true;
	return false;
}

/* Variante du parcours qui considere les positions occupees comme des
 * obstacles temporaires. Le depart n'est jamais bloque : l'appelant peut
 * laisser le personnage courant dans sa liste sans rendre le trajet
 * impossible. */
static int *bfs_from_avoid(const Map *m, int sx, int sy,
                           const int *busy_x, const int *busy_y, int nb_busy) {
	if (!map_can_stand(m, sx, sy)) return NULL;

	int n = m->w * m->h;
	int *prev = malloc(sizeof(int) * (size_t)n);
	int *queue = malloc(sizeof(int) * (size_t)n);
	for (int i = 0; i < n; i++) prev[i] = -2;

	int head = 0, tail = 0;
	queue[tail++] = sy * m->w + sx;
	prev[sy * m->w + sx] = -1;

	const int dx[4] = { 0, 0, -2, 2 };
	const int dy[4] = { -1, 1, 0, 0 };

	while (head < tail) {
		int idx = queue[head++];
		int x = idx % m->w, y = idx / m->w;
		for (int i = 0; i < 4; i++) {
			if (!map_can_step(m, x, y, dx[i], dy[i])) continue;
			int nx = x + dx[i], ny = y + dy[i];
			int nidx = ny * m->w + nx;
			if (prev[nidx] != -2) continue;
			if ((nx != sx || ny != sy) &&
			    occupied_at(nx, ny, busy_x, busy_y, nb_busy)) continue;
			prev[nidx] = idx;
			queue[tail++] = nidx;
		}
	}
	free(queue);
	return prev;
}

int map_next_step(const Map *m, int x, int y, int tx, int ty, int *nx, int *ny) {
	if (!m || (x == tx && y == ty)) return -1;

	int *prev = bfs_from(m, x, y);
	if (!prev) return -1;

	int target = ty * m->w + tx;
	if (tx < 0 || ty < 0 || tx >= m->w || ty >= m->h || prev[target] == -2) {
		free(prev);
		return -1;
	}

	/* On remonte du but jusqu'a la case qui suit immediatement le depart. */
	int cur = target;
	int start = y * m->w + x;
	while (prev[cur] != start && prev[cur] >= 0) cur = prev[cur];
	free(prev);

	*nx = cur % m->w;
	*ny = cur / m->w;
	return 0;
}

int map_next_step_avoid(const Map *m, int x, int y, int tx, int ty,
                        const int *busy_x, const int *busy_y, int nb_busy,
                        int *nx, int *ny) {
	if (!m || (x == tx && y == ty)) return -1;
	if (tx < 0 || ty < 0 || tx >= m->w || ty >= m->h) return -1;

	int *prev = bfs_from_avoid(m, x, y, busy_x, busy_y, nb_busy);
	if (!prev) return -1;

	int target = ty * m->w + tx;
	if (prev[target] == -2) {
		free(prev);
		return -1;
	}

	int cur = target;
	int start = y * m->w + x;
	while (prev[cur] != start && prev[cur] >= 0) cur = prev[cur];
	free(prev);

	*nx = cur % m->w;
	*ny = cur / m->w;
	return 0;
}

int map_path_len(const Map *m, int x, int y, int tx, int ty) {
	if (!m) return -1;
	if (x == tx && y == ty) return 0;

	int *prev = bfs_from(m, x, y);
	if (!prev) return -1;

	if (tx < 0 || ty < 0 || tx >= m->w || ty >= m->h) { free(prev); return -1; }
	int target = ty * m->w + tx;
	if (prev[target] == -2) { free(prev); return -1; }

	int len = 0;
	int cur = target;
	while (prev[cur] >= 0) { cur = prev[cur]; len++; }
	free(prev);
	return len;
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

static void build_rooms(Map *m, const StoryMap *sm) {
	m->nb_rooms = 0;
	if (!sm) return;

	for (int i = 0; i < sm->nb_rooms && m->nb_rooms < MAP_MAX_ROOMS; i++) {
		const StoryRoom *sr = &sm->rooms[i];
		/* Un point pose sur un mur ne remplirait rien : la piece resterait
		 * vide et tout ce qui s'y trouve deviendrait injoignable. */
		if (map_tile(m, sr->x, sr->y) != m->map_c_empty) continue;
		if (m->room_of[sr->y * m->w + sr->x] != ROOM_NONE) continue;

		Room *r = &m->rooms[m->nb_rooms];
		memset(r, 0, sizeof(*r));
		snprintf(r->id, sizeof(r->id), "%s", sr->id ? sr->id : "");
		snprintf(r->name, sizeof(r->name), "%s", sr->name ? sr->name : r->id);
		r->seed_x = sr->x;
		r->seed_y = sr->y;
		flood_room(m, m->nb_rooms, sr->x, sr->y);
		m->nb_rooms++;
	}
}

int map_init(Game *game, const Story *story) {
	Map *m = &game->map;
	/* Relancer une enquete reconstruit la carte : sans cette liberation, la
	 * precedente restait allouee pour rien. */
	map_free(m);
	memset(m, 0, sizeof(*m));
	m->map_c_empty = ' ';

	const StoryMap *sm = (story && story->has_map) ? &story->map : NULL;

	if (sm && sm->tiles) {
		m->w = sm->w;
		m->h = sm->h;
		m->map_len = (size_t)m->w * (size_t)m->h;
		m->map = malloc(m->map_len + 1);
		memcpy(m->map, sm->tiles, m->map_len);
	} else {
		m->w = fallback_w;
		m->h = fallback_h;
		m->map_len = (size_t)m->w * (size_t)m->h;
		m->map = malloc(m->map_len + 1);
		memcpy(m->map, fallback_map, m->map_len);
	}
	m->map[m->map_len] = '\0';

	m->room_of = malloc(sizeof(int) * m->map_len);
	for (size_t i = 0; i < m->map_len; i++) m->room_of[i] = ROOM_NONE;

	build_rooms(m, sm);
	detect_doors(m);

	/* Position de depart du joueur : celle de l'histoire si elle est jouable,
	 * sinon la premiere case libre, pour ne jamais demarrer dans un mur. */
	int sx = sm ? sm->start_x : 29;
	int sy = sm ? sm->start_y : 9;
	if (!map_can_stand(m, sx, sy)) {
		sx = sy = -1;
		for (int y = 0; y < m->h && sx < 0; y++) {
			for (int x = 0; x < m->w; x++) {
				if (map_tile(m, x, y) == m->map_c_empty) { sx = x; sy = y; break; }
			}
		}
		if (sx < 0) return -1;
	}
	game->player.x = sx;
	game->player.y = sy;
	m->x_parity = sx & 1;

	return 0;
}

void map_free(Map *m) {
	if (!m) return;
	free(m->map);
	free(m->room_of);
	m->map = NULL;
	m->room_of = NULL;
}
