#include "includes.h"
#include <time.h>
#include <math.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

uint64_t millis(){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);  // steady clock
    return ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

double distance(int x1, int y1, int x2, int y2){
    int dx = x2 - x1;
    int dy = y2 - y1;
    return sqrt(dx * dx + dy * dy);
}

int min(int a, int b){
	return (a < b ? a : b);
}

static void info_time(WINDOW *win) {
	time_t now = time(NULL);
	struct tm local;
	char stamp[16];
	localtime_r(&now, &local);
	strftime(stamp, sizeof(stamp), "%H:%M:%S", &local);
	wattron(win, COLOR_PAIR(PAIR_HEADING));
	wprintw(win, "[%s] ", stamp);
	wattroff(win, COLOR_PAIR(PAIR_HEADING));
}

static void pinfo_v(Game *game, int pair, bool colored,
		const char *fmt, va_list ap) {
	if (!game || !game->display.info) return;

	pthread_mutex_lock(&game->display.m_display_update);
	info_time(game->display.info);
	if (colored) wattron(game->display.info, COLOR_PAIR(pair));
	vw_printw(game->display.info, fmt, ap);
	if (colored) wattroff(game->display.info, COLOR_PAIR(pair));
	wrefresh(game->display.info);
	pthread_mutex_unlock(&game->display.m_display_update);
}

/* Comme pinfo, mais dans une couleur donnee. */
void pinfo_c(Game *game, int pair, const char *fmt, ...){
	va_list ap;
	va_start(ap, fmt);
	pinfo_v(game, pair, true, fmt, ap);
	va_end(ap);
}

void pinfo(Game *game, const char *fmt, ...){
	va_list ap;
	va_start(ap, fmt);
	pinfo_v(game, 0, false, fmt, ap);
	va_end(ap);
}

void pdiag(Game *game, int pair, const char *fmt, ...) {
	if (!game || !game->options.diagnostic_ingame_logs || !game->display.info) return;

	char text[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(text, sizeof(text), fmt, ap);
	va_end(ap);
	for (size_t i = 0; text[i]; i++)
		if (text[i] == '\n' || text[i] == '\r' || text[i] == '\t') text[i] = ' ';

	/* [HH:MM:SS] + "[diag] " occupent 18 colonnes. Les messages emis sont
	 * courts et majoritairement ASCII ; cette borne empeche ncurses de les
	 * replier sur une seconde ligne dans les petits terminaux. */
	int max = getmaxx(game->display.info) - 19;
	if (max < 8) max = 8;
	if ((int)strlen(text) > max) {
		int cut = max - 3;
		while (cut > 0 && (((unsigned char)text[cut] & 0xC0) == 0x80)) cut--;
		text[cut] = '\0';
		strncat(text, "...", sizeof(text) - strlen(text) - 1);
	}
	pinfo_c(game, pair, "[diag] %s\n", text);
}


#define MAX_W 256
#define MAX_H 256

typedef struct {
    int x, y, dist;
} Point;

int shortest_distance(const char *map, int map_w, int map_h,
                      int x_start, int y_start, int x_end, int y_end)
{
    if (map[y_start * map_w + x_start] == '1' ||
        map[y_end * map_w + x_end] == '1')
        return -1; // start or end inside a wall

    int visited[MAX_H][MAX_W] = {0};
    Point queue[MAX_W * MAX_H];
    int head = 0, tail = 0;

    queue[tail++] = (Point){x_start, y_start, 0};
    visited[y_start][x_start] = 1;

    int dx[4] = { 0,  0, -1, 1};
    int dy[4] = {-1,  1,  0, 0};

    while (head < tail)
    {
        Point p = queue[head++];

        if (p.x == x_end && p.y == y_end)
            return p.dist;

        for (int i = 0; i < 4; i++)
        {
            int nx = p.x + dx[i];
            int ny = p.y + dy[i];

            if (nx >= 0 && nx < map_w && ny >= 0 && ny < map_h &&
                !visited[ny][nx] && 
				map[ny * map_w + nx] != '1' && 
				map[ny * map_w + nx] != 'V' && 
				map[ny * map_w + nx] != 'H')
            {
                visited[ny][nx] = 1;
                queue[tail++] = (Point){nx, ny, p.dist + 1};
            }
        }
    }

    return -1; // no path found
}
