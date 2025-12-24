//go to >550 for the important stuff
#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <libinput.h>
#include "s7.h"
#include <linux/input-event-codes.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <errno.h>
#include <stdarg.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_alpha_modifier_v1.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_drm.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <xkbcommon/xkbcommon.h>
#ifdef XWAYLAND
#include <wlr/xwayland.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#endif

/* Cairo removed - using FreeType directly for text rendering */
#include <drm_fourcc.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include "util.h"

/* macros */
#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#define CLEANMASK(mask)         (mask & ~WLR_MODIFIER_CAPS)
#define VISIBLEON(C, M)         ((M) && (C)->mon == (M) && ((C)->tags & (M)->tagset[(M)->seltags]))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define END(A)                  ((A) + LENGTH(A))
#define TAGMASK                 ((1u << TAGCOUNT) - 1)
#define LISTEN(E, L, H)         wl_signal_add((E), ((L)->notify = (H), (L)))
#define LISTEN_STATIC(E, H)     do { struct wl_listener *_l = ecalloc(1, sizeof(*_l)); _l->notify = (H); wl_signal_add((E), _l); } while (0)

/* enums */
enum { CurNormal, CurPressed, CurMove, CurResize }; /* cursor */
enum { XDGShell, LayerShell, X11 }; /* client types */
enum { LyrBg, LyrBottom, LyrTile, LyrFloat, LyrTop, LyrFS, LyrOverlay, LyrBlock, NUM_LAYERS }; /* scene layers */
enum { DirLeft, DirRight, DirUp, DirDown }; /* directions for focus/swap */

/* forward declarations */
typedef struct DwindleNode DwindleNode;

typedef union {
	int i;
	uint32_t ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int mod;
	unsigned int button;
	void (*func)(const Arg *);
	const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct {
	/* Must keep this field first */
	unsigned int type; /* XDGShell or X11* */

	Monitor *mon;
	struct wlr_scene_tree *scene;
	struct wlr_scene_tree *scene_surface;
	struct wlr_scene_buffer *frame_top;
	struct wlr_scene_buffer *frame_bottom;
	struct wlr_scene_buffer *frame_left; 
	struct wlr_scene_buffer *frame_right;
	struct wl_list link;
	struct wl_list flink;
	struct wlr_box geom; /* layout-relative, includes border */
	struct wlr_box prev; /* layout-relative, includes border */
	struct wlr_box bounds; /* only width and height are used */
	union {
		struct wlr_xdg_surface *xdg;
		struct wlr_xwayland_surface *xwayland;
	} surface;
	struct wlr_xdg_toplevel_decoration_v1 *decoration;
	struct wl_listener commit;
	struct wl_listener map;
	struct wl_listener maximize;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener set_title;
	struct wl_listener fullscreen;
	struct wl_listener set_decoration_mode;
	struct wl_listener destroy_decoration;
#ifdef XWAYLAND
	struct wl_listener activate;
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener configure;
	struct wl_listener set_hints;
#endif
	unsigned int bw;
	uint32_t tags;
	int isfloating, isurgent, isfullscreen;
	uint32_t resize; /* configure serial of a pending resize */
	char prev_mon_name[64]; /* remember monitor name for VT switch restore */
	DwindleNode *dwindle;    /* dwindle layout node (NULL if floating) */
	int needs_title_scroll;  /* 1 if title overflows and needs scrolling */
	/* Cached frame buffers for reuse (avoid per-frame allocation) */
	struct TitleBuffer *frame_top_buf;
	struct TitleBuffer *frame_bottom_buf;
	struct TitleBuffer *frame_left_buf;
	struct TitleBuffer *frame_right_buf;
	int frame_width;  /* cached dimensions to detect resize */
	int frame_height;
} Client;

typedef struct {
	uint32_t mod;
	xkb_keysym_t keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	struct wlr_keyboard_group *wlr_group;

	int nsyms;
	const xkb_keysym_t *keysyms; /* invalid if nsyms == 0 */
	uint32_t mods; /* invalid if nsyms == 0 */
	struct wl_event_source *key_repeat_source;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
} KeyboardGroup;

typedef struct {
	/* Must keep this field first */
	unsigned int type; /* LayerShell */

	Monitor *mon;
	struct wlr_scene_tree *scene;
	struct wlr_scene_tree *popups;
	struct wlr_scene_layer_surface_v1 *scene_layer;
	struct wl_list link;
	int mapped;
	struct wlr_layer_surface_v1 *layer_surface;

	struct wl_listener destroy;
	struct wl_listener unmap;
	struct wl_listener surface_commit;
} LayerSurface;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

/* Dwindle layout binary tree node */
struct DwindleNode {
	DwindleNode *parent;
	DwindleNode *children[2];  /* NULL if leaf node (has window) */
	Client *client;            /* NULL if internal node */
	struct wlr_box box;        /* grid-aligned geometry */
	int split_horizontal;      /* 1 = split left/right, 0 = split top/bottom */
	float split_ratio;         /* 0.0-1.0, how much of space goes to children[0] */
	uint32_t tags;
	Monitor *mon;
};

/* Text buffer for title bars - defined early for Monitor struct */
struct TitleBuffer {
	struct wlr_buffer base;
	void *data;
	int stride;
};

struct Monitor {
	struct wl_list link;
	struct wlr_output *wlr_output;
	struct wlr_scene_output *scene_output;
	struct wlr_scene_rect *fullscreen_bg; /* See createmon() for info */
	struct wlr_scene_buffer *bar; /* status bar / launcher */
	struct TitleBuffer *bar_buf;  /* cached bar buffer for reuse */
	int bar_width;                /* cached width to detect resize */
	struct wl_listener frame;
	struct wl_listener destroy;
	struct wl_listener request_state;
	struct wl_listener destroy_lock_surface;
	struct wlr_session_lock_surface_v1 *lock_surface;
	struct wlr_box m; /* monitor area, layout-relative */
	struct wlr_box w; /* window area, layout-relative */
	struct wl_list layers[4]; /* LayerSurface.link */
	const Layout *lt[2];
	unsigned int seltags;
	unsigned int sellt;
	uint32_t tagset[2];
	float mfact;
	int gamma_lut_changed;
	int nmaster;
	char ltsymbol[16];
	int asleep;
};

typedef struct {
	const char *name;
	float mfact;
	int nmaster;
	float scale;
	const Layout *lt;
	enum wl_output_transform rr;
	int x, y;
} MonitorRule;

typedef struct {
	struct wlr_pointer_constraint_v1 *constraint;
	struct wl_listener destroy;
} PointerConstraint;

typedef struct {
	const char *id;
	const char *title;
	uint32_t tags;
	int isfloating;
	int monitor;
} Rule;

typedef struct {
	struct wlr_scene_tree *scene;

	struct wlr_session_lock_v1 *lock;
	struct wl_listener new_surface;
	struct wl_listener unlock;
	struct wl_listener destroy;
} SessionLock;

/* scary */
static void applybounds(Client *c, struct wlr_box *bbox);
static void applyrules(Client *c);
static void arrange(Monitor *m);
static void arrangelayer(Monitor *m, struct wl_list *list,
		struct wlr_box *usable_area, int exclusive);
static void arrangelayers(Monitor *m);
static void axisnotify(struct wl_listener *listener, void *data);
static void buttonpress(struct wl_listener *listener, void *data);
static void chvt(const Arg *arg);
static void checkidleinhibitor(struct wlr_surface *exclude);
static void cleanup(void);
static void cleanupmon(struct wl_listener *listener, void *data);
static void cleanuplisteners(void);
static void closemon(Monitor *m);
static void commitlayersurfacenotify(struct wl_listener *listener, void *data);
static void commitnotify(struct wl_listener *listener, void *data);
static void commitpopup(struct wl_listener *listener, void *data);
static void createdecoration(struct wl_listener *listener, void *data);
static void createidleinhibitor(struct wl_listener *listener, void *data);
static void createkeyboard(struct wlr_keyboard *keyboard);
static KeyboardGroup *createkeyboardgroup(void);
static void createlayersurface(struct wl_listener *listener, void *data);
static void createlocksurface(struct wl_listener *listener, void *data);
static void createmon(struct wl_listener *listener, void *data);
static void createnotify(struct wl_listener *listener, void *data);
static void createpointer(struct wlr_pointer *pointer);
static void createpointerconstraint(struct wl_listener *listener, void *data);
static void createpopup(struct wl_listener *listener, void *data);
static void cursorconstrain(struct wlr_pointer_constraint_v1 *constraint);
static void cursorframe(struct wl_listener *listener, void *data);
static void cursorwarptohint(void);
static void destroydecoration(struct wl_listener *listener, void *data);
static void destroydragicon(struct wl_listener *listener, void *data);
static void destroyidleinhibitor(struct wl_listener *listener, void *data);
static void destroylayersurfacenotify(struct wl_listener *listener, void *data);
static void destroylock(SessionLock *lock, int unlocked);
static void destroylocksurface(struct wl_listener *listener, void *data);
static void destroynotify(struct wl_listener *listener, void *data);
static void destroypointerconstraint(struct wl_listener *listener, void *data);
static void destroysessionlock(struct wl_listener *listener, void *data);
static void destroykeyboardgroup(struct wl_listener *listener, void *data);
static Monitor *dirtomon(enum wlr_direction dir);
static DwindleNode *dwindle_create(Client *c);
static void dwindle_remove(Client *c);
static void dwindle_arrange(Monitor *m, uint32_t tags);
static void dwindle_recalc(DwindleNode *node);
static DwindleNode *dwindle_find_root(Monitor *m, uint32_t tags);
static void focusdir(const Arg *arg);
static void swapdir(const Arg *arg);
static Client *client_in_direction(Client *c, int dir);
static void focusclient(Client *c, int lift);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static Client *focustop(Monitor *m);
static void fullscreennotify(struct wl_listener *listener, void *data);
static void gpureset(struct wl_listener *listener, void *data);
static void handlesig(int signo);
static int signal_fd_cb(int fd, uint32_t mask, void *data);
static void file_debug_log(const char *fmt, ...);
#define TBWM_LOG_DEBUG 0
#define TBWM_LOG_INFO 1
#define TBWM_LOG_WARN 2
#define TBWM_LOG_ERROR 3
static void tbwm_log(int level, const char *fmt, ...);

/* Simple file logger for debugging when running in a graphical session */
static void
file_debug_log(const char *fmt, ...)
{
	FILE *f = fopen("/tmp/tbwm-debug.log", "a");
	if (!f)
		return;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fflush(f);
	fclose(f);
}

static void incnmaster(const Arg *arg);
static void inputdevice(struct wl_listener *listener, void *data);
static int keybinding(uint32_t mods, xkb_keysym_t sym);
static void keypress(struct wl_listener *listener, void *data);
static void keypressmod(struct wl_listener *listener, void *data);
static int keyrepeat(void *data);
static void killclient(const Arg *arg);
static void locksession(struct wl_listener *listener, void *data);
static void mapnotify(struct wl_listener *listener, void *data);
static void maximizenotify(struct wl_listener *listener, void *data);
static void monocle(Monitor *m);
static void motionabsolute(struct wl_listener *listener, void *data);
static void motionnotify(uint32_t time, struct wlr_input_device *device, double sx,
		double sy, double sx_unaccel, double sy_unaccel);
static void motionrelative(struct wl_listener *listener, void *data);
static void moveresize(const Arg *arg);
static void outputmgrapply(struct wl_listener *listener, void *data);
static void outputmgrapplyortest(struct wlr_output_configuration_v1 *config, int test);
static void outputmgrtest(struct wl_listener *listener, void *data);
static void pointerfocus(Client *c, struct wlr_surface *surface,
		double sx, double sy, uint32_t time);
static void printstatus(void);
static void powermgrsetmode(struct wl_listener *listener, void *data);
static void quit(const Arg *arg);
static void refresh(const Arg *arg);

static void rendermon(struct wl_listener *listener, void *data);
static void requestdecorationmode(struct wl_listener *listener, void *data);
static void requeststartdrag(struct wl_listener *listener, void *data);
static void requestmonstate(struct wl_listener *listener, void *data);
static void resize(Client *c, struct wlr_box geo, int interact);
static void run(char *startup_cmd);
static void run_startup_commands(void);
static void setcursor(struct wl_listener *listener, void *data);
static void setcursorshape(struct wl_listener *listener, void *data);
static void setfloating(Client *c, int floating);
static void setfullscreen(Client *c, int fullscreen);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setmon(Client *c, Monitor *m, uint32_t newtags);
static void setpsel(struct wl_listener *listener, void *data);
static void setsel(struct wl_listener *listener, void *data);
static void setup(void);
static void setup_scheme(void);
static void load_config(void);
static void setup_foot_config(void);
static int check_scheme_bindings(uint32_t mods, xkb_keysym_t sym);
static void setupgrid(void);
static void spawn(const Arg *arg);
static void buildappcache(void);
static void render_char_to_buffer(uint32_t *pixels, int buf_w, int buf_h, int x, int y,
                      unsigned long charcode, uint32_t color);
static void render_char_clipped(uint32_t *pixels, int buf_w, int buf_h, int x, int y,
                    unsigned long charcode, uint32_t color, int clip_left, int clip_right);
static unsigned long utf8_decode(const char *s, int *pos);
static void updatebar(Monitor *m);
static void updatebars(void);
static void updateappmenu(void);
static int appmenu_item_count(void);
static int bartimer(void *data);
static int scrolltimer(void *data);
static void togglelauncher(const Arg *arg);
static void togglerepl(const Arg *arg);
static void toggleappmenu(const Arg *arg);
static void updaterepl(void);
static void repl_add_line(const char *line);
void tbwm_log(int level, const char *fmt, ...);
static void repl_eval(void);
static int replkey(xkb_keysym_t sym);
static void updateframe(Client *c);
static void updateframes(void);

static void startdrag(struct wl_listener *listener, void *data);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void tile(Monitor *m);
static void dwindle(Monitor *m);
static void togglefloating(const Arg *arg);
static void togglefullscreen(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unlocksession(struct wl_listener *listener, void *data);
static void unmaplayersurfacenotify(struct wl_listener *listener, void *data);
static void unmapnotify(struct wl_listener *listener, void *data);
static void updatemons(struct wl_listener *listener, void *data);
static void updatetitle(struct wl_listener *listener, void *data);
static void urgent(struct wl_listener *listener, void *data);
static void view(const Arg *arg);
static void virtualkeyboard(struct wl_listener *listener, void *data);
static void virtualpointer(struct wl_listener *listener, void *data);
static Monitor *xytomon(double x, double y);
static void xytonode(double x, double y, struct wlr_surface **psurface,
		Client **pc, LayerSurface **pl, double *nx, double *ny);
static void zoom(const Arg *arg);

/* variables */
static int cell_width = 8;
static int cell_height = 16;
static FT_Library ft_library;
static FT_Face ft_face;

/* Glyph cache - store pre-rendered glyphs to avoid repeated FT_Load_Char calls */
#define GLYPH_CACHE_SIZE 512
typedef struct {
	unsigned long charcode;
	int valid;
	int width;
	int rows;
	int pitch;
	int bitmap_left;
	int bitmap_top;
	unsigned char *bitmap; /* allocated buffer for glyph bitmap */
} CachedGlyph;
static CachedGlyph glyph_cache[GLYPH_CACHE_SIZE];

/* Launcher state */
static int launcher_active = 0;
static char launcher_input[256] = {0};
static int launcher_input_len = 0;
static int launcher_selection = 0;
static char **app_cache = NULL;
static int app_cache_count = 0;

/* Signal handling helpers: set by signal handler and used from main loop */
static volatile sig_atomic_t exit_requested = 0;
static int signal_fd = -1;
static struct wl_event_source *signal_fd_source = NULL;

static struct wl_event_source *bar_timer = NULL;
static uint32_t title_scroll_offset = 0; /* pixel offset for smooth title scrolling */
static int title_scroll_mode = 1;        /* 0 = truncate with ..., 1 = scroll */
static int title_scroll_speed = 30;      /* pixels per second */
static struct wl_event_source *scroll_timer = NULL;
static int any_title_needs_scroll = 0;   /* track if any title needs scrolling */

/* REPL state */
static int repl_visible = 1;             /* 1 = REPL background/text visible */
static int repl_input_active = 0;        /* 1 = REPL accepting keyboard input */
static char repl_input[1024] = {0};      /* current input line */
static int repl_input_len = 0;
#define REPL_HISTORY_LINES 256
#define REPL_LINE_LEN 256
static char repl_history[REPL_HISTORY_LINES][REPL_LINE_LEN];  /* scrollback buffer */
static int repl_history_count = 0;       /* number of lines in history */
static int repl_scroll_offset = 0;       /* scroll position (0 = bottom) */
static struct wlr_scene_buffer *repl_buffer = NULL;  /* rendered REPL output */

/* REPL log config: only lines with severity >= this appear in REPL */
static int cfg_repl_log_level = TBWM_LOG_ERROR; /* default: only errors */
/* Pipe to capture stderr and forward to REPL */
static int repl_stderr_fd = -1; /* read end */
static int repl_stderr_wfd = -1; /* write end */
static struct wl_event_source *repl_stderr_source = NULL;

/* Dwindle layout nodes */
#define MAX_DWINDLE_NODES 256
static DwindleNode dwindle_nodes[MAX_DWINDLE_NODES];
static int dwindle_node_count = 0;

/* s7 Scheme interpreter */
static s7_scheme *sc = NULL;

/* ==================== RUNTIME CONFIG (replaces config.h) ==================== */
/* These can all be modified at runtime via Scheme */

/* Appearance */
static int cfg_sloppyfocus = 1;
static int cfg_bypass_surface_visibility = 0;
static unsigned int cfg_borderpx = 1;
static int cfg_tagcount = 9;
static int cfg_show_time = 1;          /* Show time in status bar */
static int cfg_show_date = 1;          /* Show date in status bar */
static char cfg_status_text[256] = ""; /* Custom status text (overrides date/time if set) */

/* Colors (#RRGGBB format, alpha added at render time)
 * cfg_bg_color          = root/REPL background (black)
 * cfg_bg_text_color     = text on background/REPL (grey)
 * cfg_bar_color         = status bar background (blue)
 * cfg_bar_text_color    = status bar text (grey)
 * cfg_border_color      = window border/frame highlight (blue)
 * cfg_border_line_color = box-drawing characters (grey)
 * cfg_menu_color        = app menu background (grey)
 * cfg_menu_text_color   = app menu text (white)
 */
static uint32_t cfg_bg_color = 0x000000;           /* black background */
static uint32_t cfg_bg_text_color = 0xaaaaaa;      /* grey text */
static uint32_t cfg_bar_color = 0x0000aa;          /* blue bar background */
static uint32_t cfg_bar_text_color = 0xaaaaaa;     /* grey bar text */
static uint32_t cfg_border_color = 0x0000aa;       /* blue highlight */
static uint32_t cfg_border_line_color = 0xaaaaaa;  /* grey box-drawing */
static uint32_t cfg_menu_color = 0xaaaaaa;         /* grey menu background */
static uint32_t cfg_menu_text_color = 0x000000;    /* black menu text */
static char cfg_menu_button[16] = "X";             /* app menu button label */

/* App menu state */
static int appmenu_active = 0;
static struct wlr_scene_buffer *appmenu_buffer = NULL;
static struct TitleBuffer *appmenu_tb = NULL;  /* cached buffer for reuse */

/* App launcher data structures */
#define MAX_APPS 512
#define MAX_CATEGORIES 32
#define APP_NAME_LEN 64
#define APP_EXEC_LEN 256
#define CAT_NAME_LEN 32

typedef struct {
	char name[APP_NAME_LEN];
	char exec[APP_EXEC_LEN];
	char category[CAT_NAME_LEN];
} AppEntry;

typedef struct {
	char name[CAT_NAME_LEN];
	int app_count;
} CategoryEntry;

static AppEntry app_entries[MAX_APPS];
static int app_entry_count = 0;
static CategoryEntry categories[MAX_CATEGORIES];
static int category_count = 0;
static int menu_current_category = -1;  /* -1 = showing categories, >=0 = showing apps in that category */
static int menu_scroll_offset = 0;
static int menu_selected_row = 0;       /* currently selected row (keyboard nav) */
static int apps_loaded = 0;

/* Startup commands */
#define MAX_STARTUP_CMDS 32
static char *cfg_startup_cmds[MAX_STARTUP_CMDS];
static int cfg_startup_cmd_count = 0;
static int cfg_startup_ran = 0;

/* Legacy wlroots border colors (not really used with custom frames) */
static float cfg_rootcolor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
static float cfg_bordercolor[4] = {0.267f, 0.267f, 0.267f, 1.0f};
static float cfg_focuscolor[4] = {0.0f, 0.333f, 0.467f, 1.0f};
static float cfg_fullscreen_bg[4] = {0.0f, 0.0f, 0.0f, 1.0f};

/* Grid font settings */
static char cfg_font_path[512] = "/usr/share/fonts/tbwm/PxPlus_IBM_VGA_8x16.ttf";
static int cfg_font_size = 16;

/* Keyboard settings */
static int cfg_repeat_rate = 25;
static int cfg_repeat_delay = 600;

/* Trackpad settings */
static int cfg_tap_to_click = 1;
static int cfg_tap_and_drag = 1;
static int cfg_drag_lock = 1;
static int cfg_natural_scrolling = 0;
static int cfg_disable_while_typing = 1;
static int cfg_left_handed = 0;
static int cfg_middle_button_emulation = 0;
static int cfg_scroll_method = 1;  /* 0=none, 1=2fg, 2=edge, 3=on_button_down */
static int cfg_click_method = 1;   /* 0=none, 1=button_areas, 2=clickfinger */
static double cfg_accel_speed = 0.0;
static int cfg_accel_profile = 1;  /* 0=none, 1=adaptive, 2=flat */

/* Window rules - dynamic array */
#define MAX_RULES 64
typedef struct {
	char id[64];
	char title[128];
	uint32_t tags;
	int isfloating;
	int monitor;
} RuntimeRule;
static RuntimeRule cfg_rules[MAX_RULES];
static int cfg_rule_count = 0;

/* Mouse bindings (dynamic) */
typedef struct {
	uint32_t mod;
	uint32_t button;
	s7_pointer callback;
	s7_int gc_loc;
} MouseBinding;
static MouseBinding *cfg_mouse_bindings = NULL;
static int cfg_mouse_binding_count = 0;
static int cfg_mouse_binding_capacity = 0;

static void
ensure_mouse_bindings_capacity(int extra)
{
	if (cfg_mouse_binding_capacity - cfg_mouse_binding_count >= extra)
		return;
	int newcap = cfg_mouse_binding_capacity ? cfg_mouse_binding_capacity * 2 : 8;
	while (newcap - cfg_mouse_binding_count < extra)
		newcap *= 2;
	MouseBinding *tmp = realloc(cfg_mouse_bindings, newcap * sizeof(MouseBinding));
	if (!tmp) {
		tbwm_log(TBWM_LOG_ERROR, "tbwm: out of memory growing mouse bindings\n");
		return;
	}
	cfg_mouse_bindings = tmp;
	for (int i = cfg_mouse_binding_capacity; i < newcap; ++i) {
		cfg_mouse_bindings[i].callback = s7_nil(sc);
		cfg_mouse_bindings[i].gc_loc = -1;
	}
	cfg_mouse_binding_capacity = newcap;
	file_debug_log("tbwm-scm: mouse_bindings capacity grown to %d\n", cfg_mouse_binding_capacity);
}

/* Log level */
static int cfg_log_level = WLR_ERROR;

/* ==================== END RUNTIME CONFIG ==================== */

/* Debug counters for buffer leak detection */
static int titlebuf_alloc_count = 0;
static int titlebuf_free_count = 0;
static int glyph_malloc_count = 0;
static int glyph_free_count = 0;
static size_t glyph_total_bytes = 0;

static void titlebuf_destroy(struct wlr_buffer *buf) {
	struct TitleBuffer *tb = wl_container_of(buf, tb, base);
	titlebuf_free_count++;
	free(tb->data);
	free(tb);
}

static bool titlebuf_begin_data_ptr_access(struct wlr_buffer *buf,
		uint32_t flags, void **data, uint32_t *format, size_t *stride) {
	struct TitleBuffer *tb = wl_container_of(buf, tb, base);
	*data = tb->data;
	*format = DRM_FORMAT_ARGB8888;
	*stride = tb->stride;
	return true;
}

static void titlebuf_end_data_ptr_access(struct wlr_buffer *buf) {
	/* Nothing to do */
}

static const struct wlr_buffer_impl titlebuf_impl = {
	.destroy = titlebuf_destroy,
	.begin_data_ptr_access = titlebuf_begin_data_ptr_access,
	.end_data_ptr_access = titlebuf_end_data_ptr_access,
};

static pid_t child_pid = -1;
static int locked;
static void *exclusive_focus;
static struct wl_display *dpy;
static struct wl_event_loop *event_loop;
static struct wlr_backend *backend;
static struct wlr_scene *scene;
static struct wlr_scene_tree *layers[NUM_LAYERS];
static struct wlr_scene_tree *drag_icon;
/* Map from ZWLR_LAYER_SHELL_* constants to Lyr* enum */
static const int layermap[] = { LyrBg, LyrBottom, LyrTop, LyrOverlay };
static struct wlr_renderer *drw;
static struct wlr_allocator *alloc;
static struct wlr_compositor *compositor;
static struct wlr_session *session;

static struct wlr_xdg_shell *xdg_shell;
static struct wlr_xdg_activation_v1 *activation;
static struct wlr_xdg_decoration_manager_v1 *xdg_decoration_mgr;
static struct wl_list clients; /* tiling order */
static struct wl_list fstack;  /* focus order */
static struct wlr_idle_notifier_v1 *idle_notifier;
static struct wlr_idle_inhibit_manager_v1 *idle_inhibit_mgr;
static struct wlr_layer_shell_v1 *layer_shell;
static struct wlr_output_manager_v1 *output_mgr;
static struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_mgr;
static struct wlr_virtual_pointer_manager_v1 *virtual_pointer_mgr;
static struct wlr_cursor_shape_manager_v1 *cursor_shape_mgr;
static struct wlr_output_power_manager_v1 *power_mgr;

static struct wlr_pointer_constraints_v1 *pointer_constraints;
static struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr;
static struct wlr_pointer_constraint_v1 *active_constraint;

static struct wlr_cursor *cursor;
static struct wlr_xcursor_manager *cursor_mgr;

static struct wlr_scene_rect *root_bg;
static struct wlr_session_lock_manager_v1 *session_lock_mgr;
static struct wlr_scene_rect *locked_bg;
static struct wlr_session_lock_v1 *cur_lock;

static struct wlr_seat *seat;
static KeyboardGroup *kb_group;
static unsigned int cursor_mode;
static Client *grabc;
static int grabcx, grabcy; /* client-relative */

static struct wlr_output_layout *output_layout;
static struct wlr_box sgeom;
static struct wl_list mons;
static Monitor *selmon;

/* global event handlers */
static struct wl_listener cursor_axis = {.notify = axisnotify};
static struct wl_listener cursor_button = {.notify = buttonpress};
static struct wl_listener cursor_frame = {.notify = cursorframe};
static struct wl_listener cursor_motion = {.notify = motionrelative};
static struct wl_listener cursor_motion_absolute = {.notify = motionabsolute};
static struct wl_listener gpu_reset = {.notify = gpureset};
static struct wl_listener layout_change = {.notify = updatemons};
static struct wl_listener new_idle_inhibitor = {.notify = createidleinhibitor};
static struct wl_listener new_input_device = {.notify = inputdevice};
static struct wl_listener new_virtual_keyboard = {.notify = virtualkeyboard};
static struct wl_listener new_virtual_pointer = {.notify = virtualpointer};
static struct wl_listener new_pointer_constraint = {.notify = createpointerconstraint};
static struct wl_listener new_output = {.notify = createmon};
static struct wl_listener new_xdg_toplevel = {.notify = createnotify};
static struct wl_listener new_xdg_popup = {.notify = createpopup};
static struct wl_listener new_xdg_decoration = {.notify = createdecoration};
static struct wl_listener new_layer_surface = {.notify = createlayersurface};
static struct wl_listener output_mgr_apply = {.notify = outputmgrapply};
static struct wl_listener output_mgr_test = {.notify = outputmgrtest};
static struct wl_listener output_power_mgr_set_mode = {.notify = powermgrsetmode};
static struct wl_listener request_activate = {.notify = urgent};
static struct wl_listener request_cursor = {.notify = setcursor};
static struct wl_listener request_set_psel = {.notify = setpsel};
static struct wl_listener request_set_sel = {.notify = setsel};
static struct wl_listener request_set_cursor_shape = {.notify = setcursorshape};
static struct wl_listener request_start_drag = {.notify = requeststartdrag};
static struct wl_listener start_drag = {.notify = startdrag};
static struct wl_listener new_session_lock = {.notify = locksession};

#ifdef XWAYLAND
static void activatex11(struct wl_listener *listener, void *data);
static void associatex11(struct wl_listener *listener, void *data);
static void configurex11(struct wl_listener *listener, void *data);
static void createnotifyx11(struct wl_listener *listener, void *data);
static void dissociatex11(struct wl_listener *listener, void *data);
static void sethints(struct wl_listener *listener, void *data);
static void xwaylandready(struct wl_listener *listener, void *data);
static struct wl_listener new_xwayland_surface = {.notify = createnotifyx11};
static struct wl_listener xwayland_ready = {.notify = xwaylandready};
static struct wlr_xwayland *xwayland;
#endif

/* configuration, allows nested code to access above variables */
#include "config.h"

/* attempt to encapsulate suck into one file */
#include "client.h"

/* function implementations */
void
applybounds(Client *c, struct wlr_box *bbox)
{
	/* set minimum possible */
	c->geom.width = MAX(1 + 2 * (int)c->bw, c->geom.width);
	c->geom.height = MAX(1 + 2 * (int)c->bw, c->geom.height);

	if (c->geom.x >= bbox->x + bbox->width)
		c->geom.x = bbox->x + bbox->width - c->geom.width;
	if (c->geom.y >= bbox->y + bbox->height)
		c->geom.y = bbox->y + bbox->height - c->geom.height;
	if (c->geom.x + c->geom.width <= bbox->x)
		c->geom.x = bbox->x;
	if (c->geom.y + c->geom.height <= bbox->y)
		c->geom.y = bbox->y;
}

void
applyrules(Client *c)
{
	/* rule matching */
	const char *appid, *title;
	uint32_t newtags = 0;
	int i;
	const Rule *r;
	Monitor *mon = selmon, *m;

	appid = client_get_appid(c);
	title = client_get_title(c);

	for (r = rules; r < END(rules); r++) {
		if ((!r->title || strstr(title, r->title))
				&& (!r->id || strstr(appid, r->id))) {
			c->isfloating = r->isfloating;
			newtags |= r->tags;
			i = 0;
			wl_list_for_each(m, &mons, link) {
				if (r->monitor == i++)
					mon = m;
			}
		}
	}

	c->isfloating |= client_is_float_type(c);
	setmon(c, mon, newtags);
}

void
arrange(Monitor *m)
{
	Client *c;

	if (!m->wlr_output->enabled)
		return;

	wl_list_for_each(c, &clients, link) {
		if (c->mon == m) {
			wlr_scene_node_set_enabled(&c->scene->node, VISIBLEON(c, m));
			client_set_suspended(c, !VISIBLEON(c, m));
		}
	}

	wlr_scene_node_set_enabled(&m->fullscreen_bg->node,
			(c = focustop(m)) && c->isfullscreen);

	strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, LENGTH(m->ltsymbol));

	/* We move all clients (except fullscreen and unmanaged) to LyrTile while
	 * in floating layout to avoid "real" floating clients be always on top */
	wl_list_for_each(c, &clients, link) {
		if (c->mon != m || c->scene->node.parent == layers[LyrFS])
			continue;

		wlr_scene_node_reparent(&c->scene->node,
				(!m->lt[m->sellt]->arrange && c->isfloating)
						? layers[LyrTile]
						: (m->lt[m->sellt]->arrange && c->isfloating)
								? layers[LyrFloat]
								: c->scene->node.parent);
	}

	if (m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);
	motionnotify(0, NULL, 0, 0, 0, 0);
	checkidleinhibitor(NULL);
}

void
arrangelayer(Monitor *m, struct wl_list *list, struct wlr_box *usable_area, int exclusive)
{
	LayerSurface *l;
	struct wlr_box full_area = m->m;

	wl_list_for_each(l, list, link) {
		struct wlr_layer_surface_v1 *layer_surface = l->layer_surface;

		if (!layer_surface->initialized)
			continue;

		if (exclusive != (layer_surface->current.exclusive_zone > 0))
			continue;

		wlr_scene_layer_surface_v1_configure(l->scene_layer, &full_area, usable_area);
		wlr_scene_node_set_position(&l->popups->node, l->scene->node.x, l->scene->node.y);
	}
}

void
arrangelayers(Monitor *m)
{
	int i;
	struct wlr_box usable_area = m->m;
	LayerSurface *l;
	uint32_t layers_above_shell[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
	};
	if (!m->wlr_output->enabled)
		return;

	/* Arrange exclusive surfaces from top->bottom */
	for (i = 3; i >= 0; i--)
		arrangelayer(m, &m->layers[i], &usable_area, 1);

	if (!wlr_box_equal(&usable_area, &m->w)) {
		m->w = usable_area;
		arrange(m);
	}

	/* Arrange non-exlusive surfaces from top->bottom */
	for (i = 3; i >= 0; i--)
		arrangelayer(m, &m->layers[i], &usable_area, 0);

	/* Find topmost keyboard interactive layer, if such a layer exists */
	for (i = 0; i < (int)LENGTH(layers_above_shell); i++) {
		wl_list_for_each_reverse(l, &m->layers[layers_above_shell[i]], link) {
			if (locked || !l->layer_surface->current.keyboard_interactive || !l->mapped)
				continue;
			/* Deactivate the focused client. */
			focusclient(NULL, 0);
			exclusive_focus = l;
			client_notify_enter(l->layer_surface->surface, wlr_seat_get_keyboard(seat));
			return;
		}
	}
}

void
axisnotify(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits an axis event,
	 * for example when you move the scroll wheel. */
	struct wlr_pointer_axis_event *event = data;
	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
	
	/* Handle app menu scrolling */
	if (appmenu_active && selmon && event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
		int menu_x = selmon->m.x;
		int menu_y = selmon->m.y + cell_height;
		int menu_w = 25 * cell_width;
		int menu_h = 25 * cell_height;
		
		if (cursor->x >= menu_x && cursor->x < menu_x + menu_w &&
		    cursor->y >= menu_y && cursor->y < menu_y + menu_h) {
			int item_count = appmenu_item_count();
			int content_rows = 23;
			
			if (event->delta > 0) {
				/* Scroll down */
				if (menu_scroll_offset + content_rows < item_count) {
					menu_scroll_offset++;
					updateappmenu();
				}
			} else if (event->delta < 0) {
				/* Scroll up */
				if (menu_scroll_offset > 0) {
					menu_scroll_offset--;
					updateappmenu();
				}
			}
			return; /* Consume the scroll event */
		}
	}
	
	/* TODO: allow usage of scroll wheel for mousebindings, it can be implemented
	 * by checking the event's orientation and the delta of the event */
	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(seat,
			event->time_msec, event->orientation, event->delta,
			event->delta_discrete, event->source, event->relative_direction);
}

void
buttonpress(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_button_event *event = data;
	struct wlr_keyboard *keyboard;
	uint32_t mods;
	Client *c;
	const Button *b;

	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

	switch (event->state) {
	case WL_POINTER_BUTTON_STATE_PRESSED:
		cursor_mode = CurPressed;
		selmon = xytomon(cursor->x, cursor->y);
		if (locked)
			break;

		/* Check for menu button click FIRST - this should toggle the menu */
		if (selmon && cursor->y >= selmon->m.y && cursor->y < selmon->m.y + cell_height) {
			int bar_x = cursor->x - selmon->m.x;
			int btn_len = strlen(cfg_menu_button);
			int btn_cells = btn_len + 2; /* [btn] */
			int menu_btn_end = btn_cells * cell_width;
			
			if (bar_x >= 0 && bar_x < menu_btn_end) {
				/* Clicked on menu button - toggle */
				Arg a = {0};
				toggleappmenu(&a);
				return;
			}
		}

		/* Handle app menu clicks */
		if (appmenu_active && selmon) {
			int menu_x = selmon->m.x;
			int menu_y = selmon->m.y + cell_height;
			int menu_w = 25 * cell_width;
			int menu_h = 25 * cell_height;
			
			if (cursor->x >= menu_x && cursor->x < menu_x + menu_w &&
			    cursor->y >= menu_y && cursor->y < menu_y + menu_h) {
				/* Click is inside menu */
				int rel_y = cursor->y - menu_y;
				int clicked_row = rel_y / cell_height;
				
				/* Row 0 is title bar, rows 1-23 are content, row 24 is bottom */
				if (clicked_row >= 1 && clicked_row <= 23) {
					int content_row = clicked_row - 1;
					
					if (menu_current_category < 0) {
						/* Clicked on a category */
						int cat_idx = content_row + menu_scroll_offset;
						if (cat_idx < category_count) {
							menu_current_category = cat_idx;
							menu_scroll_offset = 0;
							updateappmenu();
						}
					} else {
						/* In apps view */
						if (content_row == 0) {
							/* Clicked "Back" */
							menu_current_category = -1;
							menu_scroll_offset = 0;
							updateappmenu();
						} else {
							/* Clicked on an app */
							const char *selected_cat = categories[menu_current_category].name;
							int app_idx = 0;
							int display_row = 1;
							int i;
							
							for (i = 0; i < app_entry_count; i++) {
								if (strcmp(app_entries[i].category, selected_cat) == 0) {
									if (app_idx >= menu_scroll_offset) {
										if (display_row == content_row) {
											/* Found the clicked app - launch it */
											Arg a = { .v = (const char*[]){ "/bin/sh", "-c", app_entries[i].exec, NULL } };
											spawn(&a);
											/* Close menu after launching */
											appmenu_active = 0;
											updateappmenu();
											updatebars();
											return;
										}
										display_row++;
									}
									app_idx++;
								}
							}
						}
					}
				}
				return; /* Consume the click */
			} else {
				/* Click outside menu - close it */
				appmenu_active = 0;
				updateappmenu();
				updatebars();
			}
		}

		/* Handle clicks on the status bar */
		if (selmon && cursor->y >= selmon->m.y && cursor->y < selmon->m.y + cell_height) {
			int bar_x = cursor->x - selmon->m.x;
			int x = 0;
			
			/* Menu button region: [Menu] - variable length */
			int btn_len = strlen(cfg_menu_button);
			int btn_cells = btn_len + 2; /* [btn] */
			int menu_btn_end = btn_cells * cell_width;
			
			if (bar_x >= 0 && bar_x < menu_btn_end) {
				/* Clicked on menu button */
				Arg a = {0};
				toggleappmenu(&a);
				return;
			}
			
			/* Skip separator: | */
			x = menu_btn_end + cell_width / 2 + cell_width + cell_width / 2;
			
			/* Tags region: [1] [2] [3] ... */
			int tag;
			for (tag = 0; tag < cfg_tagcount; tag++) {
				int tag_start = x;
				int tag_end = x + 3 * cell_width;
				
				if (bar_x >= tag_start && bar_x < tag_end) {
					/* Clicked on this tag */
					Arg a = {.ui = 1 << tag};
					view(&a);
					return;
				}
				
				x += 3 * cell_width + cell_width / 2; /* tag width + gap */
			}
			
			/* Skip separator after tags */
			x += cell_width / 2 + cell_width * 2;
			
			/* Window tabs region */
			int visible_count = 0;
			Client *c;
			wl_list_for_each(c, &clients, link) {
				if (VISIBLEON(c, selmon))
					visible_count++;
			}
			
			if (visible_count > 0) {
				int n = 30 * cell_width; /* reserved for date/time */
				int tab_area_width = selmon->m.width - x - n;
				if (tab_area_width < 0) tab_area_width = 0;
				
				int max_tab_chars = 20;
				int tab_width_cells = max_tab_chars + 2;
				if (visible_count * tab_width_cells * cell_width > tab_area_width) {
					tab_width_cells = tab_area_width / (visible_count * cell_width);
					if (tab_width_cells < 5) tab_width_cells = 5;
				}
				
				wl_list_for_each(c, &clients, link) {
					if (!VISIBLEON(c, selmon))
						continue;
					
					const char *title = client_get_title(c);
					if (!title) title = "?";
					
					int title_max = tab_width_cells - 2;
					int title_len = strlen(title);
					int actual_title_chars = (title_len < title_max - 1) ? title_len : title_max - 1;
					int actual_tab_width = (actual_title_chars + 2) * cell_width;
					
					int tab_start = x;
					int tab_end = x + actual_tab_width;
					
					if (bar_x >= tab_start && bar_x < tab_end) {
						/* Clicked on this tab - focus the window */
						focusclient(c, 1);
						return;
					}
					
					x += actual_tab_width;
				}
			}
			
			/* Click was on bar but not on any interactive element - ignore */
			return;
		}

		/* Change focus if the button was _pressed_ over a client */
		xytonode(cursor->x, cursor->y, NULL, &c, NULL, NULL, NULL);
		if (c && (!client_is_unmanaged(c) || client_wants_focus(c)))
			focusclient(c, 1);

		keyboard = wlr_seat_get_keyboard(seat);
		mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
		for (b = buttons; b < END(buttons); b++) {
			if (CLEANMASK(mods) == CLEANMASK(b->mod) &&
					event->button == b->button && b->func) {
				b->func(&b->arg);
				return;
			}
		}
		/* Check Scheme mouse bindings */
		for (int i = 0; i < cfg_mouse_binding_count; i++) {
			if (CLEANMASK(mods) == CLEANMASK(cfg_mouse_bindings[i].mod) &&
					event->button == cfg_mouse_bindings[i].button) {
				if (sc && cfg_mouse_bindings[i].callback)
					s7_call(sc, cfg_mouse_bindings[i].callback, s7_nil(sc));
				return;
			}
		}
		break;
	case WL_POINTER_BUTTON_STATE_RELEASED:
		/* If you released any buttons, we exit interactive move/resize mode. */
		/* TODO: should reset to the pointer focus's current setcursor */
		if (!locked && cursor_mode != CurNormal && cursor_mode != CurPressed) {
			wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
			cursor_mode = CurNormal;
			/* Snap position to grid on drop */
			if (grabc && cell_width > 0 && cell_height > 0) {
				struct wlr_box snapped = grabc->geom;
				snapped.x = (snapped.x / cell_width) * cell_width;
				snapped.y = (snapped.y / cell_height) * cell_height;
				resize(grabc, snapped, 0);
			}
			/* Drop the window off on its new monitor */
			selmon = xytomon(cursor->x, cursor->y);
			setmon(grabc, selmon, 0);
			grabc = NULL;
			return;
		}
		cursor_mode = CurNormal;
		break;
	}
	/* If the event wasn't handled by the compositor, notify the client with
	 * pointer focus that a button press has occurred */
	wlr_seat_pointer_notify_button(seat,
			event->time_msec, event->button, event->state);
}

void
chvt(const Arg *arg)
{
	if (!session) {
		tbwm_log(TBWM_LOG_ERROR, "chvt: no session available, cannot change VT to %u", arg->ui);
		return;
	}
	/* Log the request so we can see that chvt() was invoked (debug file only) */
	file_debug_log("tbwm: chvt: request to change VT to %u\n", arg->ui);
	int rc = wlr_session_change_vt(session, arg->ui);
	if (rc < 0) {
		/* Log the error so users can diagnose permission/seat issues */
		tbwm_log(TBWM_LOG_ERROR, "chvt: wlr_session_change_vt(%u) failed: %s", arg->ui, strerror(errno));
	}
}

void
checkidleinhibitor(struct wlr_surface *exclude)
{
	int inhibited = 0, unused_lx, unused_ly;
	struct wlr_idle_inhibitor_v1 *inhibitor;
	wl_list_for_each(inhibitor, &idle_inhibit_mgr->inhibitors, link) {
		struct wlr_surface *surface = wlr_surface_get_root_surface(inhibitor->surface);
		struct wlr_scene_tree *tree = surface->data;
		if (exclude != surface && (cfg_bypass_surface_visibility || (!tree
				|| wlr_scene_node_coords(&tree->node, &unused_lx, &unused_ly)))) {
			inhibited = 1;
			break;
		}
	}

	wlr_idle_notifier_v1_set_inhibited(idle_notifier, inhibited);
}

void
cleanup(void)
{
	int i;
	
	cleanuplisteners();
#ifdef XWAYLAND
	wlr_xwayland_destroy(xwayland);
	xwayland = NULL;
#endif
	wl_display_destroy_clients(dpy);
	if (child_pid > 0) {
		kill(-child_pid, SIGTERM);
		waitpid(child_pid, NULL, 0);
	}
	wlr_xcursor_manager_destroy(cursor_mgr);

	/* Clean up glyph cache bitmaps */
	for (i = 0; i < GLYPH_CACHE_SIZE; i++) {
		if (glyph_cache[i].bitmap) {
			free(glyph_cache[i].bitmap);
			glyph_cache[i].bitmap = NULL;
		}
	}

	/* Clean up all monitor bar buffers */
	Monitor *m;
	wl_list_for_each(m, &mons, link) {
		if (m->bar) {
			wlr_scene_buffer_set_buffer(m->bar, NULL);
		}
	}

	/* Clean up app menu buffer */
	if (appmenu_buffer) {
		wlr_scene_buffer_set_buffer(appmenu_buffer, NULL);
	}
	if (appmenu_tb) {
		wlr_buffer_drop(&appmenu_tb->base);
		appmenu_tb = NULL;
	}

	/* Clean up REPL buffer */
	if (repl_buffer) {
		wlr_scene_buffer_set_buffer(repl_buffer, NULL);
	}

	/* Clean up FreeType */
	if (ft_face) {
		FT_Done_Face(ft_face);
		ft_face = NULL;
	}
	if (ft_library) {
		FT_Done_FreeType(ft_library);
		ft_library = NULL;
	}

	/* Remove our signal event source and close the descriptor */
	if (signal_fd_source) {
		wl_event_source_remove(signal_fd_source);
		signal_fd_source = NULL;
	}
	if (signal_fd >= 0) {
		close(signal_fd);
		signal_fd = -1;
	}

	/* Remove REPL stderr capture */
	if (repl_stderr_source) {
		wl_event_source_remove(repl_stderr_source);
		repl_stderr_source = NULL;
	}
	if (repl_stderr_fd >= 0) {
		close(repl_stderr_fd);
		repl_stderr_fd = -1;
	}
	if (repl_stderr_wfd >= 0) {
		close(repl_stderr_wfd);
		repl_stderr_wfd = -1;
	}

	destroykeyboardgroup(&kb_group->destroy, NULL);

	/* Clean up app cache (launcher autocomplete) */
	if (app_cache) {
		for (i = 0; i < app_cache_count; i++) {
			free(app_cache[i]);
		}
		free(app_cache);
		app_cache = NULL;
		app_cache_count = 0;
	}

	/* If it's not destroyed manually, it will cause a use-after-free of wlr_seat.
	 * Destroy it until it's fixed on the wlroots side */
	wlr_backend_destroy(backend);

	wl_display_destroy(dpy);
	/* Destroy after the wayland display (when the monitors are already destroyed)
	   to avoid destroying them with an invalid scene output. */
	wlr_scene_node_destroy(&scene->tree.node);
}

void
cleanupmon(struct wl_listener *listener, void *data)
{
	Monitor *m = wl_container_of(listener, m, destroy);
	LayerSurface *l, *tmp;
	size_t i;

	/* Clean up bar buffer */
	if (m->bar) {
		wlr_scene_buffer_set_buffer(m->bar, NULL);
	}

	/* m->layers[i] are intentionally not unlinked */
	for (i = 0; i < LENGTH(m->layers); i++) {
		wl_list_for_each_safe(l, tmp, &m->layers[i], link)
			wlr_layer_surface_v1_destroy(l->layer_surface);
	}

	wl_list_remove(&m->destroy.link);
	wl_list_remove(&m->frame.link);
	wl_list_remove(&m->link);
	wl_list_remove(&m->request_state.link);
	if (m->lock_surface)
		destroylocksurface(&m->destroy_lock_surface, NULL);
	m->wlr_output->data = NULL;
	wlr_output_layout_remove(output_layout, m->wlr_output);
	wlr_scene_output_destroy(m->scene_output);

	closemon(m);
	wlr_scene_node_destroy(&m->fullscreen_bg->node);
	free(m);
}

void
cleanuplisteners(void)
{
	wl_list_remove(&cursor_axis.link);
	wl_list_remove(&cursor_button.link);
	wl_list_remove(&cursor_frame.link);
	wl_list_remove(&cursor_motion.link);
	wl_list_remove(&cursor_motion_absolute.link);
	wl_list_remove(&gpu_reset.link);
	wl_list_remove(&new_idle_inhibitor.link);
	wl_list_remove(&layout_change.link);
	wl_list_remove(&new_input_device.link);
	wl_list_remove(&new_virtual_keyboard.link);
	wl_list_remove(&new_virtual_pointer.link);
	wl_list_remove(&new_pointer_constraint.link);
	wl_list_remove(&new_output.link);
	wl_list_remove(&new_xdg_toplevel.link);
	wl_list_remove(&new_xdg_decoration.link);
	wl_list_remove(&new_xdg_popup.link);
	wl_list_remove(&new_layer_surface.link);
	wl_list_remove(&output_mgr_apply.link);
	wl_list_remove(&output_mgr_test.link);
	wl_list_remove(&output_power_mgr_set_mode.link);
	wl_list_remove(&request_activate.link);
	wl_list_remove(&request_cursor.link);
	wl_list_remove(&request_set_psel.link);
	wl_list_remove(&request_set_sel.link);
	wl_list_remove(&request_set_cursor_shape.link);
	wl_list_remove(&request_start_drag.link);
	wl_list_remove(&start_drag.link);
	wl_list_remove(&new_session_lock.link);
#ifdef XWAYLAND
	wl_list_remove(&new_xwayland_surface.link);
	wl_list_remove(&xwayland_ready.link);
#endif
}

void
closemon(Monitor *m)
{
	/* update selmon if needed and
	 * move closed monitor's clients to the focused one */
	Client *c;
	int i = 0, nmons = wl_list_length(&mons);
	tbwm_log(TBWM_LOG_WARN, "closemon: closing monitor %s", m->wlr_output->name);

	/* Release cached bar buffer */
	if (m->bar_buf) {
		if (m->bar)
			wlr_scene_buffer_set_buffer(m->bar, NULL);
		wlr_buffer_drop(&m->bar_buf->base);
		m->bar_buf = NULL;
	}

	if (!nmons) {
		selmon = NULL;
	} else if (m == selmon) {
		do /* don't switch to disabled mons */
			selmon = wl_container_of(mons.next, selmon, link);
		while (!selmon->wlr_output->enabled && i++ < nmons);

		if (!selmon->wlr_output->enabled)
			selmon = NULL;
	}

	wl_list_for_each(c, &clients, link) {
		if (c->mon == m) {
			/* Save the monitor name so we can restore when it comes back
			 * Only save if not already set (preserve original monitor) */
			if (!c->prev_mon_name[0]) {
				strncpy(c->prev_mon_name, m->wlr_output->name, sizeof(c->prev_mon_name) - 1);
				c->prev_mon_name[sizeof(c->prev_mon_name) - 1] = '\0';
			}
			if (c->isfloating && c->geom.x > m->m.width)
				resize(c, (struct wlr_box){.x = c->geom.x - m->w.width, .y = c->geom.y,
						.width = c->geom.width, .height = c->geom.height}, 0);
			setmon(c, selmon, c->tags);
		}
	}
	focusclient(focustop(selmon), 1);
	printstatus();
}

void
commitlayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *l = wl_container_of(listener, l, surface_commit);
	struct wlr_layer_surface_v1 *layer_surface = l->layer_surface;
	struct wlr_scene_tree *scene_layer = layers[layermap[layer_surface->current.layer]];
	struct wlr_layer_surface_v1_state old_state;

	if (l->layer_surface->initial_commit) {
		client_set_scale(layer_surface->surface, l->mon->wlr_output->scale);

		/* Temporarily set the layer's current state to pending
		 * so that we can easily arrange it */
		old_state = l->layer_surface->current;
		l->layer_surface->current = l->layer_surface->pending;
		arrangelayers(l->mon);
		l->layer_surface->current = old_state;
		return;
	}

	if (layer_surface->current.committed == 0 && l->mapped == layer_surface->surface->mapped)
		return;
	l->mapped = layer_surface->surface->mapped;

	if (scene_layer != l->scene->node.parent) {
		wlr_scene_node_reparent(&l->scene->node, scene_layer);
		wl_list_remove(&l->link);
		wl_list_insert(&l->mon->layers[layer_surface->current.layer], &l->link);
		wlr_scene_node_reparent(&l->popups->node, (layer_surface->current.layer
				< ZWLR_LAYER_SHELL_V1_LAYER_TOP ? layers[LyrTop] : scene_layer));
	}

	arrangelayers(l->mon);
}

void
commitnotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, commit);

	if (c->surface.xdg->initial_commit) {
		/*
		 * Get the monitor this client will be rendered on
		 * Note that if the user set a rule in which the client is placed on
		 * a different monitor based on its title, this will likely select
		 * a wrong monitor.
		 */
		applyrules(c);
		if (c->mon) {
			client_set_scale(client_surface(c), c->mon->wlr_output->scale);
		}
		setmon(c, NULL, 0); /* Make sure to reapply rules in mapnotify() */

		wlr_xdg_toplevel_set_wm_capabilities(c->surface.xdg->toplevel,
				WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
		if (c->decoration)
			requestdecorationmode(&c->set_decoration_mode, c->decoration);
		wlr_xdg_toplevel_set_size(c->surface.xdg->toplevel, 0, 0);
		return;
	}

	resize(c, c->geom, (c->isfloating && !c->isfullscreen));

	/* mark a pending resize as completed */
	if (c->resize && c->resize <= c->surface.xdg->current.configure_serial)
		c->resize = 0;
}

void
commitpopup(struct wl_listener *listener, void *data)
{
	struct wlr_surface *surface = data;
	struct wlr_xdg_popup *popup = wlr_xdg_popup_try_from_wlr_surface(surface);
	LayerSurface *l = NULL;
	Client *c = NULL;
	struct wlr_box box;
	int type = -1;

	if (!popup->base->initial_commit)
		return;

	type = toplevel_from_wlr_surface(popup->base->surface, &c, &l);
	if (!popup->parent || type < 0)
		return;
	popup->base->surface->data = wlr_scene_xdg_surface_create(
			popup->parent->data, popup->base);
	if ((l && !l->mon) || (c && !c->mon)) {
		wlr_xdg_popup_destroy(popup);
		return;
	}
	box = type == LayerShell ? l->mon->m : c->mon->w;
	box.x -= (type == LayerShell ? l->scene->node.x : c->geom.x);
	box.y -= (type == LayerShell ? l->scene->node.y : c->geom.y);
	wlr_xdg_popup_unconstrain_from_box(popup, &box);
	wl_list_remove(&listener->link);
	free(listener);
}

void
createdecoration(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel_decoration_v1 *deco = data;
	Client *c = deco->toplevel->base->data;
	c->decoration = deco;

	LISTEN(&deco->events.request_mode, &c->set_decoration_mode, requestdecorationmode);
	LISTEN(&deco->events.destroy, &c->destroy_decoration, destroydecoration);

	requestdecorationmode(&c->set_decoration_mode, deco);
}

void
createidleinhibitor(struct wl_listener *listener, void *data)
{
	struct wlr_idle_inhibitor_v1 *idle_inhibitor = data;
	LISTEN_STATIC(&idle_inhibitor->events.destroy, destroyidleinhibitor);

	checkidleinhibitor(NULL);
}

void
createkeyboard(struct wlr_keyboard *keyboard)
{
	/* Set the keymap to match the group keymap */
	wlr_keyboard_set_keymap(keyboard, kb_group->wlr_group->keyboard.keymap);

	/* Add the new keyboard to the group */
	wlr_keyboard_group_add_keyboard(kb_group->wlr_group, keyboard);
}

KeyboardGroup *
createkeyboardgroup(void)
{
	KeyboardGroup *group = ecalloc(1, sizeof(*group));
	struct xkb_context *context;
	struct xkb_keymap *keymap;

	group->wlr_group = wlr_keyboard_group_create();
	group->wlr_group->data = group;

	/* Prepare an XKB keymap and assign it to the keyboard group. */
	context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!(keymap = xkb_keymap_new_from_names(context, &xkb_rules,
				XKB_KEYMAP_COMPILE_NO_FLAGS)))
		die("failed to compile keymap");

	wlr_keyboard_set_keymap(&group->wlr_group->keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);

	wlr_keyboard_set_repeat_info(&group->wlr_group->keyboard, cfg_repeat_rate, cfg_repeat_delay);

	/* Set up listeners for keyboard events */
	LISTEN(&group->wlr_group->keyboard.events.key, &group->key, keypress);
	LISTEN(&group->wlr_group->keyboard.events.modifiers, &group->modifiers, keypressmod);

	group->key_repeat_source = wl_event_loop_add_timer(event_loop, keyrepeat, group);

	/* A seat can only have one keyboard, but this is a limitation of the
	 * Wayland protocol - not wlroots. We assign all connected keyboards to the
	 * same wlr_keyboard_group, which provides a single wlr_keyboard interface for
	 * all of them. Set this combined wlr_keyboard as the seat keyboard.
	 */
	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	return group;
}

void
createlayersurface(struct wl_listener *listener, void *data)
{
	struct wlr_layer_surface_v1 *layer_surface = data;
	LayerSurface *l;
	struct wlr_surface *surface = layer_surface->surface;
	struct wlr_scene_tree *scene_layer = layers[layermap[layer_surface->pending.layer]];

	if (!layer_surface->output
			&& !(layer_surface->output = selmon ? selmon->wlr_output : NULL)) {
		wlr_layer_surface_v1_destroy(layer_surface);
		return;
	}

	l = layer_surface->data = ecalloc(1, sizeof(*l));
	l->type = LayerShell;
	LISTEN(&surface->events.commit, &l->surface_commit, commitlayersurfacenotify);
	LISTEN(&surface->events.unmap, &l->unmap, unmaplayersurfacenotify);
	LISTEN(&layer_surface->events.destroy, &l->destroy, destroylayersurfacenotify);

	l->layer_surface = layer_surface;
	l->mon = layer_surface->output->data;
	l->scene_layer = wlr_scene_layer_surface_v1_create(scene_layer, layer_surface);
	l->scene = l->scene_layer->tree;
	l->popups = surface->data = wlr_scene_tree_create(layer_surface->current.layer
			< ZWLR_LAYER_SHELL_V1_LAYER_TOP ? layers[LyrTop] : scene_layer);
	l->scene->node.data = l->popups->node.data = l;

	wl_list_insert(&l->mon->layers[layer_surface->pending.layer],&l->link);
	wlr_surface_send_enter(surface, layer_surface->output);
}

void
createlocksurface(struct wl_listener *listener, void *data)
{
	SessionLock *lock = wl_container_of(listener, lock, new_surface);
	struct wlr_session_lock_surface_v1 *lock_surface = data;
	Monitor *m = lock_surface->output->data;
	struct wlr_scene_tree *scene_tree = lock_surface->surface->data
			= wlr_scene_subsurface_tree_create(lock->scene, lock_surface->surface);
	m->lock_surface = lock_surface;

	wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
	wlr_session_lock_surface_v1_configure(lock_surface, m->m.width, m->m.height);

	LISTEN(&lock_surface->events.destroy, &m->destroy_lock_surface, destroylocksurface);

	if (m == selmon)
		client_notify_enter(lock_surface->surface, wlr_seat_get_keyboard(seat));
}

void
createmon(struct wl_listener *listener, void *data)
{
	/* This event is raised by the backend when a new output (aka a display or
	 * monitor) becomes available. */
	struct wlr_output *wlr_output = data;
	const MonitorRule *r;
	size_t i;
	struct wlr_output_state state;
	Monitor *m;

	if (!wlr_output_init_render(wlr_output, alloc, drw))
		return;

	m = wlr_output->data = ecalloc(1, sizeof(*m));
	m->wlr_output = wlr_output;

	for (i = 0; i < LENGTH(m->layers); i++)
		wl_list_init(&m->layers[i]);

	wlr_output_state_init(&state);
	/* Initialize monitor state using configured rules */
	m->tagset[0] = m->tagset[1] = 1;
	for (r = monrules; r < END(monrules); r++) {
		if (!r->name || strstr(wlr_output->name, r->name)) {
			m->m.x = r->x;
			m->m.y = r->y;
			m->mfact = r->mfact;
			m->nmaster = r->nmaster;
			m->lt[0] = r->lt;
			m->lt[1] = &layouts[LENGTH(layouts) > 1 && r->lt != &layouts[1]];
			strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, LENGTH(m->ltsymbol));
			wlr_output_state_set_scale(&state, r->scale);
			wlr_output_state_set_transform(&state, r->rr);
			break;
		}
	}

	/* The mode is a tuple of (width, height, refresh rate), and each
	 * monitor supports only a specific set of modes. We just pick the
	 * monitor's preferred mode; a more sophisticated compositor would let
	 * the user configure it. */
	wlr_output_state_set_mode(&state, wlr_output_preferred_mode(wlr_output));

	/* Set up event listeners */
	LISTEN(&wlr_output->events.frame, &m->frame, rendermon);
	LISTEN(&wlr_output->events.destroy, &m->destroy, cleanupmon);
	LISTEN(&wlr_output->events.request_state, &m->request_state, requestmonstate);

	wlr_output_state_set_enabled(&state, 1);
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	wl_list_insert(&mons, &m->link);
	printstatus();

	/* The xdg-protocol specifies:
	 *
	 * If the fullscreened surface is not opaque, the compositor must make
	 * sure that other screen content not part of the same surface tree (made
	 * up of subsurfaces, popups or similarly coupled surfaces) are not
	 * visible below the fullscreened surface.
	 *
	 */
	/* updatemons() will resize and set correct position */
	m->fullscreen_bg = wlr_scene_rect_create(layers[LyrFS], 0, 0, cfg_fullscreen_bg);
	wlr_scene_node_set_enabled(&m->fullscreen_bg->node, 0);

	/* Adds this to the output layout in the order it was configured.
	 *
	 * The output layout utility automatically adds a wl_output global to the
	 * display, which Wayland clients can see to find out information about the
	 * output (such as DPI, scale factor, manufacturer, etc).
	 */
	m->scene_output = wlr_scene_output_create(scene, wlr_output);
	if (m->m.x == -1 && m->m.y == -1)
		wlr_output_layout_add_auto(output_layout, wlr_output);
	else
		wlr_output_layout_add(output_layout, wlr_output, m->m.x, m->m.y);
}

void
createnotify(struct wl_listener *listener, void *data)
{
	/* This event is raised when a client creates a new toplevel (application window). */
	struct wlr_xdg_toplevel *toplevel = data;
	Client *c = NULL;

	/* Allocate a Client for this surface */
	c = toplevel->base->data = ecalloc(1, sizeof(*c));
	c->surface.xdg = toplevel->base;
	c->bw = cfg_borderpx;

	LISTEN(&toplevel->base->surface->events.commit, &c->commit, commitnotify);
	LISTEN(&toplevel->base->surface->events.map, &c->map, mapnotify);
	LISTEN(&toplevel->base->surface->events.unmap, &c->unmap, unmapnotify);
	LISTEN(&toplevel->events.destroy, &c->destroy, destroynotify);
	LISTEN(&toplevel->events.request_fullscreen, &c->fullscreen, fullscreennotify);
	LISTEN(&toplevel->events.request_maximize, &c->maximize, maximizenotify);
	LISTEN(&toplevel->events.set_title, &c->set_title, updatetitle);
}

void
createpointer(struct wlr_pointer *pointer)
{
	struct libinput_device *device;
	if (wlr_input_device_is_libinput(&pointer->base)
			&& (device = wlr_libinput_get_device_handle(&pointer->base))) {

		if (libinput_device_config_tap_get_finger_count(device)) {
			libinput_device_config_tap_set_enabled(device, cfg_tap_to_click);
			libinput_device_config_tap_set_drag_enabled(device, cfg_tap_and_drag);
			libinput_device_config_tap_set_drag_lock_enabled(device, cfg_drag_lock);
			libinput_device_config_tap_set_button_map(device, LIBINPUT_CONFIG_TAP_MAP_LRM);
		}

		if (libinput_device_config_scroll_has_natural_scroll(device))
			libinput_device_config_scroll_set_natural_scroll_enabled(device, cfg_natural_scrolling);

		if (libinput_device_config_dwt_is_available(device))
			libinput_device_config_dwt_set_enabled(device, cfg_disable_while_typing);

		if (libinput_device_config_left_handed_is_available(device))
			libinput_device_config_left_handed_set(device, cfg_left_handed);

		if (libinput_device_config_middle_emulation_is_available(device))
			libinput_device_config_middle_emulation_set_enabled(device, cfg_middle_button_emulation);

		if (libinput_device_config_scroll_get_methods(device) != LIBINPUT_CONFIG_SCROLL_NO_SCROLL) {
			enum libinput_config_scroll_method sm = LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
			switch (cfg_scroll_method) {
				case 1: sm = LIBINPUT_CONFIG_SCROLL_2FG; break;
				case 2: sm = LIBINPUT_CONFIG_SCROLL_EDGE; break;
				case 3: sm = LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN; break;
			}
			libinput_device_config_scroll_set_method(device, sm);
		}

		if (libinput_device_config_click_get_methods(device) != LIBINPUT_CONFIG_CLICK_METHOD_NONE) {
			enum libinput_config_click_method cm = LIBINPUT_CONFIG_CLICK_METHOD_NONE;
			switch (cfg_click_method) {
				case 1: cm = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS; break;
				case 2: cm = LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER; break;
			}
			libinput_device_config_click_set_method(device, cm);
		}

		if (libinput_device_config_send_events_get_modes(device))
			libinput_device_config_send_events_set_mode(device, LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);

		if (libinput_device_config_accel_is_available(device)) {
			enum libinput_config_accel_profile ap = LIBINPUT_CONFIG_ACCEL_PROFILE_NONE;
			switch (cfg_accel_profile) {
				case 1: ap = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE; break;
				case 2: ap = LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT; break;
			}
			libinput_device_config_accel_set_profile(device, ap);
			libinput_device_config_accel_set_speed(device, cfg_accel_speed);
		}
	}

	wlr_cursor_attach_input_device(cursor, &pointer->base);
}

void
createpointerconstraint(struct wl_listener *listener, void *data)
{
	PointerConstraint *pointer_constraint = ecalloc(1, sizeof(*pointer_constraint));
	pointer_constraint->constraint = data;
	LISTEN(&pointer_constraint->constraint->events.destroy,
			&pointer_constraint->destroy, destroypointerconstraint);
}

void
createpopup(struct wl_listener *listener, void *data)
{
	/* This event is raised when a client (either xdg-shell or layer-shell)
	 * creates a new popup. */
	struct wlr_xdg_popup *popup = data;
	LISTEN_STATIC(&popup->base->surface->events.commit, commitpopup);
}

void
cursorconstrain(struct wlr_pointer_constraint_v1 *constraint)
{
	if (active_constraint == constraint)
		return;

	if (active_constraint)
		wlr_pointer_constraint_v1_send_deactivated(active_constraint);

	active_constraint = constraint;
	wlr_pointer_constraint_v1_send_activated(constraint);
}

void
cursorframe(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits a frame
	 * event. Frame events are sent after regular pointer events to group
	 * multiple events together. For instance, two axis events may happen at the
	 * same time, in which case a frame event won't be sent in between. */
	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(seat);
}

void
cursorwarptohint(void)
{
	Client *c = NULL;
	double sx = active_constraint->current.cursor_hint.x;
	double sy = active_constraint->current.cursor_hint.y;

	toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
	if (c && active_constraint->current.cursor_hint.enabled) {
		wlr_cursor_warp(cursor, NULL, sx + c->geom.x + c->bw, sy + c->geom.y + c->bw);
		wlr_seat_pointer_warp(active_constraint->seat, sx, sy);
	}
}

void
destroydecoration(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, destroy_decoration);

	wl_list_remove(&c->destroy_decoration.link);
	wl_list_remove(&c->set_decoration_mode.link);
}

void
destroydragicon(struct wl_listener *listener, void *data)
{
	/* Focus enter isn't sent during drag, so refocus the focused node. */
	focusclient(focustop(selmon), 1);
	motionnotify(0, NULL, 0, 0, 0, 0);
	wl_list_remove(&listener->link);
	free(listener);
}

void
destroyidleinhibitor(struct wl_listener *listener, void *data)
{
	/* `data` is the wlr_surface of the idle inhibitor being destroyed,
	 * at this point the idle inhibitor is still in the list of the manager */
	checkidleinhibitor(wlr_surface_get_root_surface(data));
	wl_list_remove(&listener->link);
	free(listener);
}

void
destroylayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *l = wl_container_of(listener, l, destroy);

	wl_list_remove(&l->link);
	wl_list_remove(&l->destroy.link);
	wl_list_remove(&l->unmap.link);
	wl_list_remove(&l->surface_commit.link);
	wlr_scene_node_destroy(&l->scene->node);
	wlr_scene_node_destroy(&l->popups->node);
	free(l);
}

void
destroylock(SessionLock *lock, int unlock)
{
	wlr_seat_keyboard_notify_clear_focus(seat);
	if ((locked = !unlock))
		goto destroy;

	wlr_scene_node_set_enabled(&locked_bg->node, 0);

	focusclient(focustop(selmon), 0);
	motionnotify(0, NULL, 0, 0, 0, 0);

destroy:
	wl_list_remove(&lock->new_surface.link);
	wl_list_remove(&lock->unlock.link);
	wl_list_remove(&lock->destroy.link);

	wlr_scene_node_destroy(&lock->scene->node);
	cur_lock = NULL;
	free(lock);
}

void
destroylocksurface(struct wl_listener *listener, void *data)
{
	Monitor *m = wl_container_of(listener, m, destroy_lock_surface);
	struct wlr_session_lock_surface_v1 *surface, *lock_surface = m->lock_surface;

	m->lock_surface = NULL;
	wl_list_remove(&m->destroy_lock_surface.link);

	if (lock_surface->surface != seat->keyboard_state.focused_surface)
		return;

	if (locked && cur_lock && !wl_list_empty(&cur_lock->surfaces)) {
		surface = wl_container_of(cur_lock->surfaces.next, surface, link);
		client_notify_enter(surface->surface, wlr_seat_get_keyboard(seat));
	} else if (!locked) {
		focusclient(focustop(selmon), 1);
	} else {
		wlr_seat_keyboard_clear_focus(seat);
	}
}

void
destroynotify(struct wl_listener *listener, void *data)
{
	/* Called when the xdg_toplevel is destroyed. */
	Client *c = wl_container_of(listener, c, destroy);
	wl_list_remove(&c->destroy.link);
	wl_list_remove(&c->set_title.link);
	wl_list_remove(&c->fullscreen.link);
#ifdef XWAYLAND
	if (c->type != XDGShell) {
		wl_list_remove(&c->activate.link);
		wl_list_remove(&c->associate.link);
		wl_list_remove(&c->configure.link);
		wl_list_remove(&c->dissociate.link);
		wl_list_remove(&c->set_hints.link);
	} else
#endif
	{
		wl_list_remove(&c->commit.link);
		wl_list_remove(&c->map.link);
		wl_list_remove(&c->unmap.link);
		wl_list_remove(&c->maximize.link);
	}
	free(c);
}

void
destroypointerconstraint(struct wl_listener *listener, void *data)
{
	PointerConstraint *pointer_constraint = wl_container_of(listener, pointer_constraint, destroy);

	if (active_constraint == pointer_constraint->constraint) {
		cursorwarptohint();
		active_constraint = NULL;
	}

	wl_list_remove(&pointer_constraint->destroy.link);
	free(pointer_constraint);
}

void
destroysessionlock(struct wl_listener *listener, void *data)
{
	SessionLock *lock = wl_container_of(listener, lock, destroy);
	destroylock(lock, 0);
}

void
destroykeyboardgroup(struct wl_listener *listener, void *data)
{
	KeyboardGroup *group = wl_container_of(listener, group, destroy);
	wl_event_source_remove(group->key_repeat_source);
	wl_list_remove(&group->key.link);
	wl_list_remove(&group->modifiers.link);
	wl_list_remove(&group->destroy.link);
	wlr_keyboard_group_destroy(group->wlr_group);
	free(group);
}

Monitor *
dirtomon(enum wlr_direction dir)
{
	struct wlr_output *next;
	if (!wlr_output_layout_get(output_layout, selmon->wlr_output))
		return selmon;
	if ((next = wlr_output_layout_adjacent_output(output_layout,
			dir, selmon->wlr_output, selmon->m.x, selmon->m.y)))
		return next->data;
	if ((next = wlr_output_layout_farthest_output(output_layout,
			dir ^ (WLR_DIRECTION_LEFT|WLR_DIRECTION_RIGHT),
			selmon->wlr_output, selmon->m.x, selmon->m.y)))
		return next->data;
	return selmon;
}

/* Allocate a new dwindle node */
static DwindleNode *
dwindle_alloc(void)
{
	DwindleNode *n;
	if (dwindle_node_count >= MAX_DWINDLE_NODES) {
		tbwm_log(TBWM_LOG_ERROR, "dwindle: out of nodes\n");
		return NULL;
	}
	n = &dwindle_nodes[dwindle_node_count++];
	memset(n, 0, sizeof(*n));
	return n;
}

/* Free a dwindle node (swap with last, decrement count) */
static void
dwindle_free(DwindleNode *node)
{
	int idx;
	DwindleNode *last;
	
	if (!node) return;
	idx = node - dwindle_nodes;
	if (idx < 0 || idx >= dwindle_node_count) return;
	
	/* If not the last node, swap with last */
	if (idx < dwindle_node_count - 1) {
		last = &dwindle_nodes[dwindle_node_count - 1];
		/* Update references to the last node */
		if (last->parent) {
			if (last->parent->children[0] == last)
				last->parent->children[0] = node;
			else if (last->parent->children[1] == last)
				last->parent->children[1] = node;
		}
		if (last->children[0]) last->children[0]->parent = node;
		if (last->children[1]) last->children[1]->parent = node;
		if (last->client) last->client->dwindle = node;
		
		*node = *last;
	}
	dwindle_node_count--;
}

/* Find the root node for a monitor/tags combination */
DwindleNode *
dwindle_find_root(Monitor *m, uint32_t tags)
{
	int i;
	DwindleNode *n;
	
	for (i = 0; i < dwindle_node_count; i++) {
		n = &dwindle_nodes[i];
		if (n->mon == m && (n->tags & tags) && n->parent == NULL)
			return n;
	}
	return NULL;
}

/* Recursively calculate node geometry and apply to windows */
void
dwindle_recalc(DwindleNode *node)
{
	DwindleNode *c0, *c1;
	int split_size;
	float ratio;
	
	if (!node) return;
	
	if (node->client) {
		/* Leaf node: apply geometry to client */
		resize(node->client, node->box, 0);
	} else {
		/* Internal node: split and recurse */
		c0 = node->children[0];
		c1 = node->children[1];
		if (!c0 || !c1) return;
		
		/* Use split_ratio (default 0.5 if not set) */
		ratio = node->split_ratio;
		if (ratio < 0.1f) ratio = 0.5f;
		if (ratio > 0.9f) ratio = 0.9f;
		
		if (node->split_horizontal) {
			/* Split left/right with 1-cell overlap for shared border */
			split_size = (int)(node->box.width * ratio);
			/* Snap to grid */
			split_size = (split_size / cell_width) * cell_width;
			if (split_size < cell_width * 3) split_size = cell_width * 3;
			if (split_size > node->box.width - cell_width * 3)
				split_size = node->box.width - cell_width * 3;
			
			c0->box.x = node->box.x;
			c0->box.y = node->box.y;
			c0->box.width = split_size;
			c0->box.height = node->box.height;
			
			/* Second child overlaps by 1 cell */
			c1->box.x = node->box.x + split_size - cell_width;
			c1->box.y = node->box.y;
			c1->box.width = node->box.width - split_size + cell_width;
			c1->box.height = node->box.height;
		} else {
			/* Split top/bottom with 1-cell overlap */
			split_size = (int)(node->box.height * ratio);
			split_size = (split_size / cell_height) * cell_height;
			if (split_size < cell_height * 3) split_size = cell_height * 3;
			if (split_size > node->box.height - cell_height * 3)
				split_size = node->box.height - cell_height * 3;
			
			c0->box.x = node->box.x;
			c0->box.y = node->box.y;
			c0->box.width = node->box.width;
			c0->box.height = split_size;
			
			c1->box.x = node->box.x;
			c1->box.y = node->box.y + split_size - cell_height;
			c1->box.width = node->box.width;
			c1->box.height = node->box.height - split_size + cell_height;
		}
		
		c0->mon = node->mon;
		c0->tags = node->tags;
		c1->mon = node->mon;
		c1->tags = node->tags;
		
		dwindle_recalc(c0);
		dwindle_recalc(c1);
	}
}

/* Create a dwindle node for a new client, splitting the focused window */
DwindleNode *
dwindle_create(Client *c)
{
	DwindleNode *node, *root, *target, *search, *new_parent;
	Client *focused;
	
	if (!c || !c->mon) return NULL;
	
	node = dwindle_alloc();
	if (!node) return NULL;
	
	node->client = c;
	node->mon = c->mon;
	node->tags = c->tags;
	c->dwindle = node;
	
	/* Find existing root for this monitor/tags */
	root = dwindle_find_root(c->mon, c->tags);
	
	if (!root) {
		/* First window on this tag: becomes root, gets full area */
		node->box = c->mon->w;
		node->parent = NULL;
		return node;
	}
	
	target = NULL;
	
	/* Find the previously focused window (not the new one) by walking fstack */
	wl_list_for_each(focused, &fstack, flink) {
		if (focused != c && VISIBLEON(focused, c->mon) && 
		    !focused->isfloating && focused->dwindle) {
			target = focused->dwindle;
			break;
		}
	}
	
	/* If no focused window found, find any leaf node */
	if (!target) {
		search = root;
		while (search && search->children[0])
			search = search->children[0];
		if (search && search->client && search->client != c)
			target = search;
	}
	
	if (!target) {
		/* Somehow no target found, just use root's geometry */
		node->box = root->box;
		return node;
	}
	
	/* Create a new parent node to hold both target and new node */
	new_parent = dwindle_alloc();
	if (!new_parent) {
		/* Out of nodes, just use existing layout */
		node->box = target->box;
		return node;
	}
	
	new_parent->box = target->box;
	new_parent->mon = target->mon;
	new_parent->tags = target->tags;
	new_parent->parent = target->parent;
	
	/* Decide split direction: horizontal if wider, vertical if taller */
	new_parent->split_horizontal = (target->box.width >= target->box.height);
	
	/* Link new parent into tree */
	if (target->parent) {
		if (target->parent->children[0] == target)
			target->parent->children[0] = new_parent;
		else
			target->parent->children[1] = new_parent;
	}
	
	/* Set up children: existing window first, new window second */
	new_parent->children[0] = target;
	new_parent->children[1] = node;
	target->parent = new_parent;
	node->parent = new_parent;
	new_parent->client = NULL;  /* internal node */
	
	return node;
}

/* Remove a client from the dwindle tree */
void
dwindle_remove(Client *c)
{
	DwindleNode *node, *parent, *sibling;
	
	if (!c || !c->dwindle) return;
	
	node = c->dwindle;
	parent = node->parent;
	c->dwindle = NULL;
	
	if (!parent) {
		/* This was the root - just free it */
		dwindle_free(node);
		return;
	}
	
	/* Find sibling */
	sibling = (parent->children[0] == node) 
		? parent->children[1] : parent->children[0];
	
	if (!sibling) {
		/* No sibling, just remove parent too */
		if (parent->parent) {
			if (parent->parent->children[0] == parent)
				parent->parent->children[0] = NULL;
			else
				parent->parent->children[1] = NULL;
		}
		dwindle_free(parent);
		dwindle_free(node);
		return;
	}
	
	/* Sibling takes over parent's position */
	sibling->box = parent->box;
	sibling->parent = parent->parent;
	
	if (parent->parent) {
		if (parent->parent->children[0] == parent)
			parent->parent->children[0] = sibling;
		else
			parent->parent->children[1] = sibling;
	}
	
	dwindle_free(parent);
	dwindle_free(node);
}

/* Arrange all dwindle windows on a monitor */
void
dwindle_arrange(Monitor *m, uint32_t tags)
{
	DwindleNode *root;
	int i;
	DwindleNode *n;

	/* First, update all dwindle nodes to match their client's current monitor/tags */
	for (i = 0; i < dwindle_node_count; i++) {
		n = &dwindle_nodes[i];
		if (n->client) {
			n->mon = n->client->mon;
			n->tags = n->client->tags;
		}
	}

	/* Propagate monitor/tags up the tree for internal nodes */
	for (i = 0; i < dwindle_node_count; i++) {
		n = &dwindle_nodes[i];
		if (!n->client && n->children[0]) {
			n->mon = n->children[0]->mon;
			n->tags = n->children[0]->tags;
		}
	}

	root = dwindle_find_root(m, tags);
	if (root) {
		root->box = m->w;
		dwindle_recalc(root);
	}
}

/* Find client in the given direction from c */
Client *
client_in_direction(Client *c, int dir)
{
	Client *best = NULL, *other;
	int best_ratio = 0;
	int best_pos = INT_MAX;
	int overlap, pos, ratio, candidate_size;
	int ax, ay, aw, ah;
	int bx, by, bw, bh;
	
	if (!c || !c->mon) return NULL;
	
	ax = c->geom.x;
	ay = c->geom.y;
	aw = c->geom.width;
	ah = c->geom.height;
	
	wl_list_for_each(other, &clients, link) {
		if (other == c || !VISIBLEON(other, c->mon) || other->isfloating || other->isfullscreen)
			continue;
		
		bx = other->geom.x;
		by = other->geom.y;
		bw = other->geom.width;
		bh = other->geom.height;
		
		overlap = 0;
		pos = 0;
		candidate_size = 1;
		
		switch (dir) {
		case DirLeft:
			if (bx + bw <= ax + aw/2 && bx < ax) {
				overlap = MIN(ay + ah, by + bh) - MAX(ay, by);
				pos = by;
				candidate_size = bh;
			}
			break;
		case DirRight:
			if (bx >= ax + aw/2 && bx + bw > ax + aw) {
				overlap = MIN(ay + ah, by + bh) - MAX(ay, by);
				pos = by;
				candidate_size = bh;
			}
			break;
		case DirUp:
			if (by + bh <= ay + ah/2 && by < ay) {
				overlap = MIN(ax + aw, bx + bw) - MAX(ax, bx);
				pos = bx;
				candidate_size = bw;
			}
			break;
		case DirDown:
			if (by >= ay + ah/2 && by + bh > ay + ah) {
				overlap = MIN(ax + aw, bx + bw) - MAX(ax, bx);
				pos = bx;
				candidate_size = bw;
			}
			break;
		}
		
		if (overlap > 0) {
			ratio = (overlap * 100) / candidate_size;
			if (!best || ratio > best_ratio || (ratio == best_ratio && pos < best_pos)) {
				best_ratio = ratio;
				best_pos = pos;
				best = other;
			}
		}
	}
	
	return best;
}

/* Focus window in direction */
void
focusdir(const Arg *arg)
{
	Client *sel, *next;
	
	if (!selmon) return;
	sel = focustop(selmon);
	if (!sel) return;
	
	next = client_in_direction(sel, arg->i);
	if (next)
		focusclient(next, 1);
}

/* Move window in direction - Hyprland style: remove, find target, re-insert splitting target */
void
swapdir(const Arg *arg)
{
	Client *sel, *target, *sibling_client;
	DwindleNode *target_node, *node, *new_parent, *parent, *sibling;
	int dir_horizontal, is_child0;
	
	if (!selmon) return;
	sel = focustop(selmon);
	if (!sel || sel->isfloating || !sel->dwindle) return;
	
	node = sel->dwindle;
	dir_horizontal = (arg->i == DirLeft || arg->i == DirRight);
	
	/* Find target window in direction */
	target = client_in_direction(sel, arg->i);
	
	if (!target || !target->dwindle) {
		/* No window in that direction */
		parent = node->parent;
		if (!parent) return;
		
		is_child0 = (parent->children[0] == node);
		sibling = is_child0 ? parent->children[1] : parent->children[0];
		if (!sibling) return;
		
		/* Find the sibling's client (might be nested) */
		while (sibling && !sibling->client && sibling->children[0])
			sibling = sibling->children[0];
		if (!sibling || !sibling->client) return;
		sibling_client = sibling->client;
		
		/* If split direction matches movement direction and we're moving "outward", absorb */
		if (parent->split_horizontal == dir_horizontal) {
			if ((arg->i == DirLeft && !is_child0) || (arg->i == DirRight && is_child0) ||
			    (arg->i == DirUp && !is_child0) || (arg->i == DirDown && is_child0)) {
				dwindle_remove(sibling_client);
				arrange(selmon);
				focusclient(sel, 1);
				return;
			}
		}
		
		/* Split direction differs from movement - change the split orientation */
		/* Remove sel, then re-insert with new split direction */
		dwindle_remove(sel);
		
		/* Re-fetch sibling after removal */
		sibling = sibling_client->dwindle;
		if (!sibling) {
			arrange(selmon);
			return;
		}
		
		/* Create node for sel */
		node = dwindle_alloc();
		if (!node) {
			arrange(selmon);
			return;
		}
		node->client = sel;
		node->mon = sel->mon;
		node->tags = sel->tags;
		sel->dwindle = node;
		
		/* Create new parent with the new split direction */
		new_parent = dwindle_alloc();
		if (!new_parent) {
			arrange(selmon);
			return;
		}
		
		new_parent->box = sibling->box;
		new_parent->mon = sibling->mon;
		new_parent->tags = sibling->tags;
		new_parent->parent = sibling->parent;
		new_parent->client = NULL;
		new_parent->split_horizontal = dir_horizontal;
		
		/* Link new parent into tree */
		if (sibling->parent) {
			if (sibling->parent->children[0] == sibling)
				sibling->parent->children[0] = new_parent;
			else
				sibling->parent->children[1] = new_parent;
		}
		
		/* Order: sel goes in the direction we moved */
		if (arg->i == DirLeft || arg->i == DirUp) {
			new_parent->children[0] = node;
			new_parent->children[1] = sibling;
		} else {
			new_parent->children[0] = sibling;
			new_parent->children[1] = node;
		}
		
		sibling->parent = new_parent;
		node->parent = new_parent;
		
		arrange(selmon);
		focusclient(sel, 1);
		return;
	}
	
	/* Remove sel from tree - target->dwindle may move due to dwindle_free swap */
	dwindle_remove(sel);
	
	/* Re-fetch target_node after removal (it may have moved in array) */
	target_node = target->dwindle;
	if (!target_node) {
		arrange(selmon);
		return;
	}
	
	/* Now insert sel by splitting target_node */
	node = dwindle_alloc();
	if (!node) {
		arrange(selmon);
		return;
	}
	
	node->client = sel;
	node->mon = sel->mon;
	node->tags = sel->tags;
	sel->dwindle = node;
	
	/* Create new parent to hold both target and sel */
	new_parent = dwindle_alloc();
	if (!new_parent) {
		node->box = target_node->box;
		arrange(selmon);
		return;
	}
	
	new_parent->box = target_node->box;
	new_parent->mon = target_node->mon;
	new_parent->tags = target_node->tags;
	new_parent->parent = target_node->parent;
	new_parent->client = NULL;
	
	/* Decide split direction based on move direction */
	if (dir_horizontal)
		new_parent->split_horizontal = 1;
	else
		new_parent->split_horizontal = 0;
	
	/* Link new parent into tree */
	if (target_node->parent) {
		if (target_node->parent->children[0] == target_node)
			target_node->parent->children[0] = new_parent;
		else
			target_node->parent->children[1] = new_parent;
	}
	
	/* Order children based on direction: sel goes in direction we moved FROM */
	if (arg->i == DirLeft || arg->i == DirUp) {
		new_parent->children[0] = node;
		new_parent->children[1] = target_node;
	} else {
		new_parent->children[0] = target_node;
		new_parent->children[1] = node;
	}
	
	target_node->parent = new_parent;
	node->parent = new_parent;
	
	/* Re-arrange to apply new positions */
	arrange(selmon);
	focusclient(sel, 1);
}

void
focusclient(Client *c, int lift)
{
	struct wlr_surface *old = seat->keyboard_state.focused_surface;
	int unused_lx, unused_ly, old_client_type;
	Client *old_c = NULL;
	LayerSurface *old_l = NULL;

	if (locked)
		return;

	/* Raise client in stacking order if requested */
	if (c && lift) {
		wlr_scene_node_raise_to_top(&c->scene->node);
		/* Ensure floating windows stay above tiled windows */
		if (!c->isfloating) {
			Client *f;
			wl_list_for_each(f, &fstack, flink) {
				if (f->isfloating && VISIBLEON(f, c->mon))
					wlr_scene_node_raise_to_top(&f->scene->node);
			}
		}
	}

	if (c && client_surface(c) == old)
		return;

	if ((old_client_type = toplevel_from_wlr_surface(old, &old_c, &old_l)) == XDGShell) {
		struct wlr_xdg_popup *popup, *tmp;
		wl_list_for_each_safe(popup, tmp, &old_c->surface.xdg->popups, link)
			wlr_xdg_popup_destroy(popup);
	}

	/* Put the new client atop the focus stack and select its monitor */
	if (c && !client_is_unmanaged(c)) {
		wl_list_remove(&c->flink);
		wl_list_insert(&fstack, &c->flink);
		selmon = c->mon;
		c->isurgent = 0;

		/* Don't change border color if there is an exclusive focus or we are
		 * handling a drag operation */
		if (!exclusive_focus && !seat->drag) {
			client_set_border_color(c, cfg_focuscolor);
			updateframe(c);
		}
	}

	/* Deactivate old client if focus is changing */
	if (old && (!c || client_surface(c) != old)) {
		/* If an overlay is focused, don't focus or activate the client,
		 * but only update its position in fstack to render its border with focuscolor
		 * and focus it after the overlay is closed. */
		if (old_client_type == LayerShell && wlr_scene_node_coords(
					&old_l->scene->node, &unused_lx, &unused_ly)
				&& old_l->layer_surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
			return;
		} else if (old_c && old_c == exclusive_focus && client_wants_focus(old_c)) {
			return;
		/* Don't deactivate old client if the new one wants focus, as this causes issues with winecfg
		 * and probably other clients */
		} else if (old_c && !client_is_unmanaged(old_c) && (!c || !client_wants_focus(c))) {
			client_set_border_color(old_c, cfg_bordercolor);
			updateframe(old_c);
			client_activate_surface(old, 0);
		}
	}
	printstatus();
	updatebars();

	if (!c) {
		/* With no client, all we have left is to clear focus */
		wlr_seat_keyboard_notify_clear_focus(seat);
		return;
	}

	/* Change cursor surface */
	motionnotify(0, NULL, 0, 0, 0, 0);

	/* Have a client, so focus its top-level wlr_surface */
	client_notify_enter(client_surface(c), wlr_seat_get_keyboard(seat));

	/* Activate the new client */
	client_activate_surface(client_surface(c), 1);
}

void
focusmon(const Arg *arg)
{
	int i = 0, nmons = wl_list_length(&mons);
	if (nmons) {
		do /* don't switch to disabled mons */
			selmon = dirtomon(arg->i);
		while (!selmon->wlr_output->enabled && i++ < nmons);
	}
	focusclient(focustop(selmon), 1);
}

void
focusstack(const Arg *arg)
{
	/* Focus the next or previous client (in tiling order) on selmon */
	Client *c, *sel = focustop(selmon);
	if (!sel || (sel->isfullscreen && !client_has_children(sel)))
		return;
	if (arg->i > 0) {
		wl_list_for_each(c, &sel->link, link) {
			if (&c->link == &clients)
				continue; /* wrap past the sentinel node */
			if (VISIBLEON(c, selmon))
				break; /* found it */
		}
	} else {
		wl_list_for_each_reverse(c, &sel->link, link) {
			if (&c->link == &clients)
				continue; /* wrap past the sentinel node */
			if (VISIBLEON(c, selmon))
				break; /* found it */
		}
	}
	/* If only one client is visible on selmon, then c == sel */
	focusclient(c, 1);
}

/* We probably should change the name of this: it sounds like it
 * will focus the topmost client of this mon, when actually will
 * only return that client */
Client *
focustop(Monitor *m)
{
	Client *c;
	wl_list_for_each(c, &fstack, flink) {
		if (VISIBLEON(c, m))
			return c;
	}
	return NULL;
}

void
fullscreennotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, fullscreen);
	setfullscreen(c, client_wants_fullscreen(c));
}

void
gpureset(struct wl_listener *listener, void *data)
{
	struct wlr_renderer *old_drw = drw;
	struct wlr_allocator *old_alloc = alloc;
	struct Monitor *m;
	if (!(drw = wlr_renderer_autocreate(backend)))
		die("couldn't recreate renderer");

	if (!(alloc = wlr_allocator_autocreate(backend, drw)))
		die("couldn't recreate allocator");

	wl_list_remove(&gpu_reset.link);
	wl_signal_add(&drw->events.lost, &gpu_reset);

	wlr_compositor_set_renderer(compositor, drw);

	wl_list_for_each(m, &mons, link) {
		wlr_output_init_render(m->wlr_output, alloc, drw);
	}

	wlr_allocator_destroy(old_alloc);
	wlr_renderer_destroy(old_drw);
}

void
handlesig(int signo)
{
	if (signo == SIGCHLD) {
		/* Reap children (async-signal-safe) */
		while (waitpid(-1, NULL, WNOHANG) > 0);
		/* Wake main loop so it can handle reaping/logging */
		if (signal_fd >= 0) {
			uint64_t one = 1;
			ssize_t s = write(signal_fd, &one, sizeof(one));
			(void)s;
		}
	} else if (signo == SIGINT || signo == SIGTERM) {
		/* Mark shutdown requested and wake main loop to perform cleanup */
		exit_requested = 1;
		if (signal_fd >= 0) {
			uint64_t one = 1;
			ssize_t s = write(signal_fd, &one, sizeof(one));
			(void)s;
		}
	}
}

static int
signal_fd_cb(int fd, uint32_t mask, void *data)
{
	uint64_t val;
	ssize_t r = read(fd, &val, sizeof(val));
	if (r < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			tbwm_log(TBWM_LOG_ERROR, "tbwm: read(signal_fd) failed: %s\n", strerror(errno));
	}
	if (exit_requested)
		quit(NULL);
	return 1; /* continue watching */
}

/* Callback that reads captured stderr and forwards WARNING/ERROR lines to the REPL.
 * We avoid calling tbwm_log() here to prevent a feedback loop into the pipe. */
static int
repl_stderr_cb(int fd, uint32_t mask, void *data)
{
	char buf[4096];
	ssize_t r;
	static char partial[8192];
	static size_t partial_len = 0;

	for (;;) {
		r = read(fd, buf, sizeof(buf));
		if (r < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			/* Otherwise, log and stop watching */
			file_debug_log("tbwm: repl_stderr_cb read() failed: %s\n", strerror(errno));
			return 1;
		}
		if (r == 0) {
			/* EOF - pipe closed */
			return 1;
		}

		/* Append to partial buffer */
		if (partial_len + (size_t)r >= sizeof(partial) - 1) {
			/* overflow - flush what we have */
			partial[partial_len] = '\0';
			/* write to debug file */
			file_debug_log("%s\n", partial);
			/* if it looks like a WARN/ERROR, send to REPL */
			if (strstr(partial, "ERROR") || strstr(partial, "error") || strstr(partial, "err:") || strstr(partial, "failed") || strstr(partial, "WARN") || strstr(partial, "warn"))
				repl_add_line(partial);
			partial_len = 0;
		}

		memcpy(partial + partial_len, buf, r);
		partial_len += r;
		partial[partial_len] = '\0';

		/* Process full lines */
		char *line_start = partial;
		char *nl;
		while ((nl = strchr(line_start, '\n')) != NULL) {
			*nl = '\0';
			/* Trim trailing carriage return */
			if (nl > line_start && nl[-1] == '\r')
				nl[-1] = '\0';
			/* Write to debug file */
			file_debug_log("%s\n", line_start);
			/* Detect severity and forward to REPL if level >= cfg_repl_log_level */
			int sev = TBWM_LOG_INFO;
			if (strstr(line_start, "ERROR") || strstr(line_start, "error") || strstr(line_start, "err:") || strstr(line_start, "failed"))
				sev = TBWM_LOG_ERROR;
			else if (strstr(line_start, "WARN") || strstr(line_start, "warn"))
				sev = TBWM_LOG_WARN;
			if (sev >= cfg_repl_log_level)
				repl_add_line(line_start);

			/* Move to next line */
			line_start = nl + 1;
		}

		/* Move any remaining partial to front */
		if (line_start != partial) {
			size_t remaining = partial + partial_len - line_start;
			memmove(partial, line_start, remaining);
			partial_len = remaining;
			partial[partial_len] = '\0';
		}
	}

	return 1; /* keep watching */
}

void
incnmaster(const Arg *arg)
{
	if (!arg || !selmon)
		return;
	selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
	arrange(selmon);
}

void
inputdevice(struct wl_listener *listener, void *data)
{
	/* This event is raised by the backend when a new input device becomes
	 * available. */
	struct wlr_input_device *device = data;
	uint32_t caps;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		createkeyboard(wlr_keyboard_from_input_device(device));
		break;
	case WLR_INPUT_DEVICE_POINTER:
		createpointer(wlr_pointer_from_input_device(device));
		break;
	default:
		/* TODO handle other input device types */
		break;
	}

	/* We need to let the wlr_seat know what our capabilities are, which is
	 * communiciated to the client. In tbwm we always have a cursor, even if
	 * there are no pointer devices, so we always include that capability. */
	/* TODO do we actually require a cursor? */
	caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&kb_group->wlr_group->devices))
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	wlr_seat_set_capabilities(seat, caps);
}

static int
launcher_match_count(void)
{
	int i, count = 0;
	for (i = 0; i < app_cache_count; i++) {
		if (strncmp(app_cache[i], launcher_input, launcher_input_len) == 0)
			count++;
	}
	return count;
}

static char *
launcher_get_match(int index)
{
	int i, count = 0;
	for (i = 0; i < app_cache_count; i++) {
		if (strncmp(app_cache[i], launcher_input, launcher_input_len) == 0) {
			if (count == index)
				return app_cache[i];
			count++;
		}
	}
	return NULL;
}

static void
runlauncher(void)
{
	char *match;
	char cmd[256];
	pid_t pid;
	FILE *log;

	if (launcher_input_len == 0)
		return;

	/* Get selected match or fall back to input - copy it before resetting */
	match = launcher_get_match(launcher_selection);
	if (match)
		strncpy(cmd, match, sizeof(cmd) - 1);
	else
		strncpy(cmd, launcher_input, sizeof(cmd) - 1);
	cmd[sizeof(cmd) - 1] = '\0';

	/* Log the launch attempt */
	log = fopen("/tmp/tbwm-launcher.log", "a");
	if (log) {
		fprintf(log, "Launching: '%s'\n", cmd);
		fclose(log);
	}

	/* Close launcher */
	launcher_active = 0;
	launcher_input[0] = '\0';
	launcher_input_len = 0;
	launcher_selection = 0;
	updatebars();

	/* Run the command via shell (like startup_cmd does) */
	pid = fork();
	if (pid == 0) {
		int fd;
		close(STDIN_FILENO);
		fd = open("/dev/null", O_RDONLY);
		(void)fd;

		/* Redirect stdout/stderr to log file for debugging */
		fd = open("/tmp/tbwm-launcher.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
		if (fd >= 0) {
			dup2(fd, STDOUT_FILENO);
			dup2(fd, STDERR_FILENO);
			close(fd);
		}

		tbwm_log(TBWM_LOG_INFO, "Child env: DISPLAY=%s WAYLAND_DISPLAY=%s\n",
			getenv("DISPLAY") ? getenv("DISPLAY") : "(none)",
			getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "(none)");
		fflush(NULL);

		setsid();
		execl("/bin/sh", "/bin/sh", "-c", cmd, NULL);
		perror("execl failed");
		_exit(EXIT_FAILURE);
	} else if (pid > 0) {
		log = fopen("/tmp/tbwm-launcher.log", "a");
		if (log) {
			fprintf(log, "Forked child PID: %d\n", pid);
			fclose(log);
		}
	} else {
		log = fopen("/tmp/tbwm-launcher.log", "a");
		if (log) {
			fprintf(log, "Fork failed!\n");
			fclose(log);
		}
	}
}

/* Get total visible items in current menu view */
static int
appmenu_item_count(void)
{
	if (menu_current_category < 0) {
		return category_count;
	} else {
		/* Count apps in this category + 1 for "Back" */
		const char *selected_cat = categories[menu_current_category].name;
		int count = 1; /* Back item */
		int i;
		for (i = 0; i < app_entry_count; i++) {
			if (strcmp(app_entries[i].category, selected_cat) == 0)
				count++;
		}
		return count;
	}
}

static int
appmenukey(xkb_keysym_t sym)
{
	int item_count = appmenu_item_count();
	int content_rows = 23; /* menu_cells_h - 2 */
	
	if (sym == XKB_KEY_Escape) {
		if (menu_current_category >= 0) {
			/* Go back to categories */
			menu_current_category = -1;
			menu_scroll_offset = 0;
			menu_selected_row = 0;
			updateappmenu();
		} else {
			/* Close menu */
			appmenu_active = 0;
			updateappmenu();
			updatebars();
		}
		return 1;
	}
	
	if (sym == XKB_KEY_Up || sym == XKB_KEY_k) {
		if (menu_selected_row > 0) {
			menu_selected_row--;
		} else if (menu_scroll_offset > 0) {
			menu_scroll_offset--;
		}
		updateappmenu();
		return 1;
	}
	
	if (sym == XKB_KEY_Down || sym == XKB_KEY_j) {
		int max_row = (item_count < content_rows) ? item_count - 1 : content_rows - 1;
		if (menu_selected_row < max_row && menu_selected_row + menu_scroll_offset < item_count - 1) {
			menu_selected_row++;
		} else if (menu_selected_row + menu_scroll_offset < item_count - 1) {
			menu_scroll_offset++;
		}
		updateappmenu();
		return 1;
	}
	
	if (sym == XKB_KEY_Return || sym == XKB_KEY_Right || sym == XKB_KEY_l) {
		int selected_idx = menu_selected_row + menu_scroll_offset;
		
		if (menu_current_category < 0) {
			/* Select a category */
			if (selected_idx < category_count) {
				menu_current_category = selected_idx;
				menu_scroll_offset = 0;
				menu_selected_row = 0;
				updateappmenu();
			}
		} else {
			/* In apps view */
			if (selected_idx == 0) {
				/* Back */
				menu_current_category = -1;
				menu_scroll_offset = 0;
				menu_selected_row = 0;
				updateappmenu();
			} else {
				/* Launch app */
				const char *selected_cat = categories[menu_current_category].name;
				int app_idx = 0;
				int target = selected_idx - 1; /* -1 for Back row */
				int i;
				
				for (i = 0; i < app_entry_count; i++) {
					if (strcmp(app_entries[i].category, selected_cat) == 0) {
						if (app_idx == target) {
							Arg a = { .v = (const char*[]){ "/bin/sh", "-c", app_entries[i].exec, NULL } };
							spawn(&a);
							appmenu_active = 0;
							updateappmenu();
							updatebars();
							return 1;
						}
						app_idx++;
					}
				}
			}
		}
		return 1;
	}
	
	if (sym == XKB_KEY_Left || sym == XKB_KEY_h || sym == XKB_KEY_BackSpace) {
		if (menu_current_category >= 0) {
			/* Go back to categories */
			menu_current_category = -1;
			menu_scroll_offset = 0;
			menu_selected_row = 0;
			updateappmenu();
		}
		return 1;
	}
	
	/* Don't consume unhandled keys - allows M-x toggle to work */
	return 0;
}

static int
launcherkey(xkb_keysym_t sym)
{
	int match_count;

	if (sym == XKB_KEY_Escape) {
		launcher_active = 0;
		launcher_input[0] = '\0';
		launcher_input_len = 0;
		launcher_selection = 0;
		updatebars();
		return 1;
	}

	if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
		runlauncher();
		return 1;
	}

	if (sym == XKB_KEY_BackSpace) {
		if (launcher_input_len > 0) {
			launcher_input[--launcher_input_len] = '\0';
			launcher_selection = 0;
			updatebars();
		}
		return 1;
	}

	/* Navigation: Tab to cycle forward, Shift+Tab to go back */
	if (sym == XKB_KEY_Tab) {
		match_count = launcher_match_count();
		if (match_count > 0) {
			launcher_selection = (launcher_selection + 1) % match_count;
			updatebars();
		}
		return 1;
	}

	if (sym == XKB_KEY_ISO_Left_Tab) { /* Shift+Tab */
		match_count = launcher_match_count();
		if (match_count > 0) {
			launcher_selection = (launcher_selection - 1 + match_count) % match_count;
			updatebars();
		}
		return 1;
	}

	/* Regular character input */
	if (sym >= 0x20 && sym <= 0x7e &&
	    launcher_input_len < (int)sizeof(launcher_input) - 1) {
		launcher_input[launcher_input_len++] = (char)sym;
		launcher_input[launcher_input_len] = '\0';
		launcher_selection = 0;
		updatebars();

		/* Auto-run if only one match */
		if (launcher_match_count() == 1) {
			runlauncher();
		}
		return 1;
	}

	return 1; /* Consume all keys in launcher mode */
}

int
keybinding(uint32_t mods, xkb_keysym_t sym)
{
	/*
	 * Here we handle compositor keybindings. This is when the compositor is
	 * processing keys, rather than passing them on to the client for its own
	 * processing.
	 */
	const Key *k;

	/* Handle launcher mode first */
	if (launcher_active)
		return launcherkey(sym);

	/* Handle REPL input mode */
	if (repl_input_active)
		return replkey(sym);

	/* Handle app menu navigation - if appmenukey handles it, return 1;
	 * otherwise fall through to check scheme bindings (e.g., M-x to toggle) */
	if (appmenu_active && appmenukey(sym))
		return 1;

	/*
	 * Fallback: some keyboards or keymaps produce plain F1..F12 for Ctrl+Alt+Fx
	 * instead of the XF86Switch_VT_* keysyms. Handle that scenario here so VT
	 * switching works regardless of the specific keysym produced.
	 */
	if (CLEANMASK(mods) == (WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT)) {
		/* Prefer XF86Switch_VT_1..12 if the keymap provides them */
		if (sym >= XKB_KEY_XF86Switch_VT_1 && sym <= XKB_KEY_XF86Switch_VT_12) {
			int n = sym - XKB_KEY_XF86Switch_VT_1 + 1;
			Arg a = {.ui = (unsigned int)n};
			tbwm_log(TBWM_LOG_INFO, "tbwm: chvt: XF86Switch_VT_%d detected, invoking chvt()\n", n);
			chvt(&a);
			return 1;
		}

		/* Older / simpler keymaps may produce plain F1..F12 */
		if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12) {
			int n = sym - XKB_KEY_F1 + 1;
			Arg a = {.ui = (unsigned int)n};
			tbwm_log(TBWM_LOG_INFO, "tbwm: chvt: F%d detected (no XF86 keysym), invoking chvt()\n", n);
			chvt(&a);
			return 1;
		}
	}

	/* Check Scheme keybindings first */
	if (check_scheme_bindings(CLEANMASK(mods), sym))
		return 1;

	/* Fallback: treat Win+Shift+Arrow as Swap (same as Win+Shift+h/j/k/l) if no Scheme binding exists for M-S-Arrow */
	if ((mods & WLR_MODIFIER_SHIFT) && (mods & (WLR_MODIFIER_LOGO|WLR_MODIFIER_ALT))) {
		if (sym == XKB_KEY_Left) {
			file_debug_log("tbwm: fallback: M-S-Left -> swapdir\n");
			Arg a = { .i = DirLeft };
			swapdir(&a);
			return 1;
		} else if (sym == XKB_KEY_Right) {
			file_debug_log("tbwm: fallback: M-S-Right -> swapdir\n");
			Arg a = { .i = DirRight };
			swapdir(&a);
			return 1;
		} else if (sym == XKB_KEY_Up) {
			file_debug_log("tbwm: fallback: M-S-Up -> swapdir\n");
			Arg a = { .i = DirUp };
			swapdir(&a);
			return 1;
		} else if (sym == XKB_KEY_Down) {
			file_debug_log("tbwm: fallback: M-S-Down -> swapdir\n");
			Arg a = { .i = DirDown };
			swapdir(&a);
			return 1;
		}
	}

	for (k = keys; k < END(keys); k++) {
		if (CLEANMASK(mods) == CLEANMASK(k->mod)
				&& sym == k->keysym && k->func) {
			tbwm_log(TBWM_LOG_INFO, "tbwm: keybinding matched mods=0x%x sym=0x%x func=%p\n", mods, sym, (void*)k->func);
			k->func(&k->arg);
			return 1;
		}
	}
	return 0;
}

void
keypress(struct wl_listener *listener, void *data)
{
	int i;
	/* This event is raised when a key is pressed or released. */
	KeyboardGroup *group = wl_container_of(listener, group, key);
	struct wlr_keyboard_key_event *event = data;

	/* Translate libinput keycode -> xkbcommon */
	uint32_t keycode = event->keycode + 8;
	/* Get a list of keysyms based on the keymap for this keyboard */
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(
			group->wlr_group->keyboard.xkb_state, keycode, &syms);

	int handled = 0;
	uint32_t mods = wlr_keyboard_get_modifiers(&group->wlr_group->keyboard);

	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

	/* Debug: log keys when Ctrl+Alt are held so we can see what keysym is generated */
	if ((mods & WLR_MODIFIER_CTRL) && (mods & WLR_MODIFIER_ALT)) {
		int k;
		for (k = 0; k < nsyms; k++) {
			char name[64] = {0};
			xkb_keysym_t s = syms[k];
			xkb_keysym_get_name(s, name, sizeof(name));
			file_debug_log("tbwm: keypress: ctrl+alt keycode=%u sym=0x%x (%s) state=%d\n",
				keycode, (unsigned int)s, name[0] ? name : "(unknown)", event->state);
			fflush(NULL);
		}
	}

	/* Debug: log keys when MODKEY (Logo or Alt) + Shift are held so we can trace M-S-Arrow bindings */
	if ((mods & WLR_MODIFIER_SHIFT) && (mods & (WLR_MODIFIER_LOGO|WLR_MODIFIER_ALT))) {
		int k;
		for (k = 0; k < nsyms; k++) {
			char name[64] = {0};
			xkb_keysym_t s = syms[k];
			xkb_keysym_get_name(s, name, sizeof(name));
			file_debug_log("tbwm: keypress: mod+shift mods=0x%x keycode=%u sym=0x%x (%s) state=%d\n",
				mods, keycode, (unsigned int)s, name[0] ? name : "(unknown)", event->state);
			fflush(NULL);
		}
	}

	/* On _press_ if there is no active screen locker,
	 * attempt to process a compositor keybinding. */
	if (!locked && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		/* Debugging: if Ctrl+Alt are held, print detected keysyms */
		if ((mods & (WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT)) == (WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT)) {
			for (i = 0; i < nsyms; i++) {
				char symname[64] = {0};
				xkb_keysym_get_name(syms[i], symname, sizeof(symname));
				file_debug_log("tbwm: keypress: mods=0x%x keycode=%u sym=0x%x (%s)\n",
					mods, keycode, (unsigned int)syms[i], symname[0] ? symname : "(no-name)");
				fflush(NULL);
			}
		}

		for (i = 0; i < nsyms; i++)
			handled = keybinding(mods, syms[i]) || handled;
	}

	if (handled && group->wlr_group->keyboard.repeat_info.delay > 0) {
		group->mods = mods;
		group->keysyms = syms;
		group->nsyms = nsyms;
		wl_event_source_timer_update(group->key_repeat_source,
				group->wlr_group->keyboard.repeat_info.delay);
	} else {
		group->nsyms = 0;
		wl_event_source_timer_update(group->key_repeat_source, 0);
	}

	if (handled)
		return;

	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	/* Pass unhandled keycodes along to the client. */
	wlr_seat_keyboard_notify_key(seat, event->time_msec,
			event->keycode, event->state);
}

void
keypressmod(struct wl_listener *listener, void *data)
{
	/* This event is raised when a modifier key, such as shift or alt, is
	 * pressed. We simply communicate this to the client. */
	KeyboardGroup *group = wl_container_of(listener, group, modifiers);

	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	/* Send modifiers to the client. */
	wlr_seat_keyboard_notify_modifiers(seat,
			&group->wlr_group->keyboard.modifiers);
}

int
keyrepeat(void *data)
{
	KeyboardGroup *group = data;
	int i;
	if (!group->nsyms || group->wlr_group->keyboard.repeat_info.rate <= 0)
		return 0;

	wl_event_source_timer_update(group->key_repeat_source,
			1000 / group->wlr_group->keyboard.repeat_info.rate);

	for (i = 0; i < group->nsyms; i++)
		keybinding(group->mods, group->keysyms[i]);

	return 0;
}

void
killclient(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		client_send_close(sel);
}

void
locksession(struct wl_listener *listener, void *data)
{
	struct wlr_session_lock_v1 *session_lock = data;
	SessionLock *lock;
	wlr_scene_node_set_enabled(&locked_bg->node, 1);
	if (cur_lock) {
		wlr_session_lock_v1_destroy(session_lock);
		return;
	}
	lock = session_lock->data = ecalloc(1, sizeof(*lock));
	focusclient(NULL, 0);

	lock->scene = wlr_scene_tree_create(layers[LyrBlock]);
	cur_lock = lock->lock = session_lock;
	locked = 1;

	LISTEN(&session_lock->events.new_surface, &lock->new_surface, createlocksurface);
	LISTEN(&session_lock->events.destroy, &lock->destroy, destroysessionlock);
	LISTEN(&session_lock->events.unlock, &lock->unlock, unlocksession);

	wlr_session_lock_v1_send_locked(session_lock);
}

void
mapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is mapped, or ready to display on-screen. */
	Client *p = NULL;
	Client *w, *c = wl_container_of(listener, c, map);
	Monitor *m;

	/* Create scene tree for this client */
	c->scene = client_surface(c)->data = wlr_scene_tree_create(layers[LyrTile]);
	/* Enabled later by a call to arrange() */
	wlr_scene_node_set_enabled(&c->scene->node, client_is_unmanaged(c));
	c->scene_surface = c->type == XDGShell
			? wlr_scene_xdg_surface_create(c->scene, c->surface.xdg)
			: wlr_scene_subsurface_tree_create(c->scene, client_surface(c));
	c->scene->node.data = c->scene_surface->node.data = c;

	client_get_geometry(c, &c->geom);

	/* Handle unmanaged clients first so we can return prior create borders */
	if (client_is_unmanaged(c)) {
		/* Unmanaged clients always are floating */
		wlr_scene_node_reparent(&c->scene->node, layers[LyrFloat]);
		wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
		client_set_size(c, c->geom.width, c->geom.height);
		if (client_wants_focus(c)) {
			focusclient(c, 1);
			exclusive_focus = c;
		}
		goto unset_fullscreen;
	}

	/* Frame buffers will be created by updateframe() */
	c->frame_top = NULL;
	c->frame_bottom = NULL;
	c->frame_left = NULL;
	c->frame_right = NULL;
	c->dwindle = NULL;

	/* Initialize client geometry with room for text frame (1 cell each side) */
	client_set_tiled(c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
	c->geom.width += 2 * cell_width;
	c->geom.height += 2 * cell_height;

	/* Insert this client into client lists (at end for tiling order, front for focus) */
	wl_list_insert(clients.prev, &c->link);
	wl_list_insert(&fstack, &c->flink);

	/* Set initial monitor, tags, floating status, and focus:
	 * we always consider floating, clients that have parent and thus
	 * we set the same tags and monitor as its parent.
	 * If there is no parent, apply rules */
	if ((p = client_get_parent(c))) {
		c->isfloating = 1;
		setmon(c, p->mon, p->tags);
	} else {
		applyrules(c);
	}
	printstatus();

unset_fullscreen:
	m = c->mon ? c->mon : xytomon(c->geom.x, c->geom.y);
	wl_list_for_each(w, &clients, link) {
		if (w != c && w != p && w->isfullscreen && m == w->mon && (w->tags & c->tags))
			setfullscreen(w, 0);
	}
}

void
maximizenotify(struct wl_listener *listener, void *data)
{
	/* This event is raised when a client would like to maximize itself,
	 * typically because the user clicked on the maximize button on
	 * client-side decorations. tbwm doesn't support maximization, but
	 * to conform to xdg-shell protocol we still must send a configure.
	 * Since xdg-shell protocol v5 we should ignore request of unsupported
	 * capabilities, just schedule a empty configure when the client uses <5
	 * protocol version
	 * wlr_xdg_surface_schedule_configure() is used to send an empty reply. */
	Client *c = wl_container_of(listener, c, maximize);
	if (c->surface.xdg->initialized
			&& wl_resource_get_version(c->surface.xdg->toplevel->resource)
					< XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
		wlr_xdg_surface_schedule_configure(c->surface.xdg);
}

void
monocle(Monitor *m)
{
	Client *c;
	int n = 0;

	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
			continue;
		resize(c, m->w, 0);
		n++;
	}
	if (n)
		snprintf(m->ltsymbol, LENGTH(m->ltsymbol), "[%d]", n);
	if ((c = focustop(m)))
		wlr_scene_node_raise_to_top(&c->scene->node);
}

void
motionabsolute(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits an _absolute_
	 * motion event, from 0..1 on each axis. This happens, for example, when
	 * wlroots is running under a Wayland window rather than KMS+DRM, and you
	 * move the mouse over the window. You could enter the window from any edge,
	 * so we have to warp the mouse there. Also, some hardware emits these events. */
	struct wlr_pointer_motion_absolute_event *event = data;
	double lx, ly, dx, dy;

	if (!event->time_msec) /* this is 0 with virtual pointers */
		wlr_cursor_warp_absolute(cursor, &event->pointer->base, event->x, event->y);

	wlr_cursor_absolute_to_layout_coords(cursor, &event->pointer->base, event->x, event->y, &lx, &ly);
	dx = lx - cursor->x;
	dy = ly - cursor->y;
	motionnotify(event->time_msec, &event->pointer->base, dx, dy, dx, dy);
}

void
motionnotify(uint32_t time, struct wlr_input_device *device, double dx, double dy,
		double dx_unaccel, double dy_unaccel)
{
	double sx = 0, sy = 0, sx_confined, sy_confined;
	Client *c = NULL, *w = NULL;
	LayerSurface *l = NULL;
	struct wlr_surface *surface = NULL;
	struct wlr_pointer_constraint_v1 *constraint;

	/* Find the client under the pointer and send the event along. */
	xytonode(cursor->x, cursor->y, &surface, &c, NULL, &sx, &sy);

	if (cursor_mode == CurPressed && !seat->drag
			&& surface != seat->pointer_state.focused_surface
			&& toplevel_from_wlr_surface(seat->pointer_state.focused_surface, &w, &l) >= 0) {
		c = w;
		surface = seat->pointer_state.focused_surface;
		sx = cursor->x - (l ? l->scene->node.x : w->geom.x);
		sy = cursor->y - (l ? l->scene->node.y : w->geom.y);
	}

	/* time is 0 in internal calls meant to restore pointer focus. */
	if (time) {
		wlr_relative_pointer_manager_v1_send_relative_motion(
				relative_pointer_mgr, seat, (uint64_t)time * 1000,
				dx, dy, dx_unaccel, dy_unaccel);

		wl_list_for_each(constraint, &pointer_constraints->constraints, link)
			cursorconstrain(constraint);

		if (active_constraint && cursor_mode != CurResize && cursor_mode != CurMove) {
			toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
			if (c && active_constraint->surface == seat->pointer_state.focused_surface) {
				sx = cursor->x - c->geom.x - c->bw;
				sy = cursor->y - c->geom.y - c->bw;
				if (wlr_region_confine(&active_constraint->region, sx, sy,
						sx + dx, sy + dy, &sx_confined, &sy_confined)) {
					dx = sx_confined - sx;
					dy = sy_confined - sy;
				}

				if (active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED)
					return;
			}
		}

		wlr_cursor_move(cursor, device, dx, dy);
		wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

		/* Update selmon (even while dragging a window) */
		if (sloppyfocus)
			selmon = xytomon(cursor->x, cursor->y);
		
		/* Update menu hover selection */
		if (appmenu_active && selmon) {
			int menu_x = selmon->m.x;
			int menu_y = selmon->m.y + cell_height;
			int menu_w = 25 * cell_width;
			int menu_h = 25 * cell_height;
			
			if (cursor->x >= menu_x && cursor->x < menu_x + menu_w &&
			    cursor->y >= menu_y && cursor->y < menu_y + menu_h) {
				int rel_y = (int)(cursor->y - menu_y);
				int hovered_row = rel_y / cell_height;
				
				/* Row 0 is title bar, rows 1-23 are content */
				if (hovered_row >= 1 && hovered_row <= 23) {
					int new_selected = hovered_row - 1; /* 0-indexed content row */
					if (new_selected != menu_selected_row) {
						menu_selected_row = new_selected;
						updateappmenu();
					}
				}
			}
		}
	}

	/* Update drag icon's position */
	wlr_scene_node_set_position(&drag_icon->node, (int)round(cursor->x), (int)round(cursor->y));

	/* If we are currently grabbing the mouse, handle and return */
	if (cursor_mode == CurMove) {
		/* Move the grabbed client to the new position, snapped to grid */
		int new_x = (int)round(cursor->x) - grabcx;
		int new_y = (int)round(cursor->y) - grabcy;
		new_x = (new_x / cell_width) * cell_width;
		new_y = (new_y / cell_height) * cell_height;
		resize(grabc, (struct wlr_box){.x = new_x, .y = new_y,
			.width = grabc->geom.width, .height = grabc->geom.height}, 1);
		return;
	} else if (cursor_mode == CurResize) {
		if (grabc->isfloating) {
			/* Floating: direct resize */
			resize(grabc, (struct wlr_box){.x = grabc->geom.x, .y = grabc->geom.y,
				.width = (int)round(cursor->x) - grabc->geom.x, .height = (int)round(cursor->y) - grabc->geom.y}, 1);
		} else if (grabc->dwindle && grabc->dwindle->parent) {
			/* Tiled: adjust split ratio of parent node */
			DwindleNode *parent = grabc->dwindle->parent;
			DwindleNode *root;
			int is_first_child = (parent->children[0] == grabc->dwindle);
			float new_ratio;
			
			if (parent->split_horizontal) {
				/* Horizontal split: adjust based on cursor X relative to parent box */
				int cursor_rel = (int)round(cursor->x) - parent->box.x;
				new_ratio = (float)cursor_rel / (float)parent->box.width;
			} else {
				/* Vertical split: adjust based on cursor Y relative to parent box */
				int cursor_rel = (int)round(cursor->y) - parent->box.y;
				new_ratio = (float)cursor_rel / (float)parent->box.height;
			}
			
			/* If we're resizing the second child, invert the ratio interpretation */
			if (!is_first_child) {
				/* Cursor position is now the end of child[0], so ratio is still correct */
			}
			
			/* Clamp ratio */
			if (new_ratio < 0.1f) new_ratio = 0.1f;
			if (new_ratio > 0.9f) new_ratio = 0.9f;
			
			parent->split_ratio = new_ratio;
			
			/* Find root and recalculate entire tree */
			root = parent;
			while (root->parent) root = root->parent;
			dwindle_recalc(root);
		}
		return;
	}

	/* If there's no client surface under the cursor, set the cursor image to a
	 * default. This is what makes the cursor image appear when you move it
	 * off of a client or over its border. */
	if (!surface && !seat->drag)
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

	pointerfocus(c, surface, sx, sy, time);
}

void
motionrelative(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits a _relative_
	 * pointer motion event (i.e. a delta) */
	struct wlr_pointer_motion_event *event = data;
	/* The cursor doesn't move unless we tell it to. The cursor automatically
	 * handles constraining the motion to the output layout, as well as any
	 * special configuration applied for the specific input device which
	 * generated the event. You can pass NULL for the device if you want to move
	 * the cursor around without any input. */
	motionnotify(event->time_msec, &event->pointer->base, event->delta_x, event->delta_y,
			event->unaccel_dx, event->unaccel_dy);
}

void
moveresize(const Arg *arg)
{
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	xytonode(cursor->x, cursor->y, NULL, &grabc, NULL, NULL, NULL);
	if (!grabc || client_is_unmanaged(grabc) || grabc->isfullscreen)
		return;

	switch (cursor_mode = arg->ui) {
	case CurMove:
		/* Calculate grab offset BEFORE making floating (which may shrink window) */
		grabcx = (int)round(cursor->x) - grabc->geom.x;
		grabcy = (int)round(cursor->y) - grabc->geom.y;
		if (!grabc->isfloating) {
			/* Make floating, then restore position so cursor stays at same spot */
			setfloating(grabc, 1);
			/* Reposition so cursor stays at same relative position */
			grabc->geom.x = (int)round(cursor->x) - grabcx;
			grabc->geom.y = (int)round(cursor->y) - grabcy;
			/* Update grab offset for new (possibly smaller) size */
			if (grabcx > grabc->geom.width) grabcx = grabc->geom.width / 2;
			if (grabcy > grabc->geom.height) grabcy = grabc->geom.height / 2;
			resize(grabc, grabc->geom, 1);
		}
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "all-scroll");
		break;
	case CurResize:
		/* Resizing: for floating windows, resize directly
		 * For tiled windows, adjust split ratios */
		if (grabc->isfloating) {
			wlr_cursor_warp_closest(cursor, NULL,
					grabc->geom.x + grabc->geom.width,
					grabc->geom.y + grabc->geom.height);
		}
		/* Store initial geometry for calculating delta */
		grabcx = grabc->geom.width;
		grabcy = grabc->geom.height;
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "se-resize");
		break;
	}
}

void
outputmgrapply(struct wl_listener *listener, void *data)
{
	struct wlr_output_configuration_v1 *config = data;
	outputmgrapplyortest(config, 0);
}

void
outputmgrapplyortest(struct wlr_output_configuration_v1 *config, int test)
{
	/*
	 * Called when a client such as wlr-randr requests a change in output
	 * configuration. This is only one way that the layout can be changed,
	 * so any Monitor information should be updated by updatemons() after an
	 * output_layout.change event, not here.
	 */
	struct wlr_output_configuration_head_v1 *config_head;
	int ok = 1;

	wl_list_for_each(config_head, &config->heads, link) {
		struct wlr_output *wlr_output = config_head->state.output;
		Monitor *m = wlr_output->data;
		struct wlr_output_state state;

		/* Ensure displays previously disabled by wlr-output-power-management-v1
		 * are properly handled*/
		m->asleep = 0;

		wlr_output_state_init(&state);
		wlr_output_state_set_enabled(&state, config_head->state.enabled);
		if (!config_head->state.enabled)
			goto apply_or_test;

		if (config_head->state.mode)
			wlr_output_state_set_mode(&state, config_head->state.mode);
		else
			wlr_output_state_set_custom_mode(&state,
					config_head->state.custom_mode.width,
					config_head->state.custom_mode.height,
					config_head->state.custom_mode.refresh);

		wlr_output_state_set_transform(&state, config_head->state.transform);
		wlr_output_state_set_scale(&state, config_head->state.scale);
		wlr_output_state_set_adaptive_sync_enabled(&state,
				config_head->state.adaptive_sync_enabled);

apply_or_test:
		ok &= test ? wlr_output_test_state(wlr_output, &state)
				: wlr_output_commit_state(wlr_output, &state);

		/* Don't move monitors if position wouldn't change. This avoids
		 * wlroots marking the output as manually configured.
		 * wlr_output_layout_add does not like disabled outputs */
		if (!test && wlr_output->enabled && (m->m.x != config_head->state.x || m->m.y != config_head->state.y))
			wlr_output_layout_add(output_layout, wlr_output,
					config_head->state.x, config_head->state.y);

		wlr_output_state_finish(&state);
	}

	if (ok)
		wlr_output_configuration_v1_send_succeeded(config);
	else
		wlr_output_configuration_v1_send_failed(config);
	wlr_output_configuration_v1_destroy(config);

	/* https://codeberg.org/tbwm/tbwm/issues/577 */
	updatemons(NULL, NULL);
}

void
outputmgrtest(struct wl_listener *listener, void *data)
{
	struct wlr_output_configuration_v1 *config = data;
	outputmgrapplyortest(config, 1);
}

void
pointerfocus(Client *c, struct wlr_surface *surface, double sx, double sy,
		uint32_t time)
{
	struct timespec now;

	if (surface != seat->pointer_state.focused_surface &&
			cfg_sloppyfocus && time && c && !client_is_unmanaged(c))
		focusclient(c, 0);

	/* If surface is NULL, clear pointer focus */
	if (!surface) {
		wlr_seat_pointer_notify_clear_focus(seat);
		return;
	}

	if (!time) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		time = now.tv_sec * 1000 + now.tv_nsec / 1000000;
	}

	/* Let the client know that the mouse cursor has entered one
	 * of its surfaces, and make keyboard focus follow if desired.
	 * wlroots makes this a no-op if surface is already focused */
	wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
	wlr_seat_pointer_notify_motion(seat, time, sx, sy);
}

void
printstatus(void)
{
	Monitor *m = NULL;
	Client *c;
	uint32_t occ, urg, sel;

	wl_list_for_each(m, &mons, link) {
		occ = urg = 0;
		wl_list_for_each(c, &clients, link) {
			if (c->mon != m)
				continue;
			occ |= c->tags;
			if (c->isurgent)
				urg |= c->tags;
		}
		if ((c = focustop(m))) {
			printf("%s title %s\n", m->wlr_output->name, client_get_title(c));
			printf("%s appid %s\n", m->wlr_output->name, client_get_appid(c));
			printf("%s fullscreen %d\n", m->wlr_output->name, c->isfullscreen);
			printf("%s floating %d\n", m->wlr_output->name, c->isfloating);
			sel = c->tags;
		} else {
			printf("%s title \n", m->wlr_output->name);
			printf("%s appid \n", m->wlr_output->name);
			printf("%s fullscreen \n", m->wlr_output->name);
			printf("%s floating \n", m->wlr_output->name);
			sel = 0;
		}

		printf("%s selmon %u\n", m->wlr_output->name, m == selmon);
		printf("%s tags %"PRIu32" %"PRIu32" %"PRIu32" %"PRIu32"\n",
			m->wlr_output->name, occ, m->tagset[m->seltags], sel, urg);
		printf("%s layout %s\n", m->wlr_output->name, m->ltsymbol);
	}
	fflush(stdout);
}

void
powermgrsetmode(struct wl_listener *listener, void *data)
{
	struct wlr_output_power_v1_set_mode_event *event = data;
	struct wlr_output_state state = {0};
	Monitor *m = event->output->data;

	if (!m)
		return;

	m->gamma_lut_changed = 1; /* Reapply gamma LUT when re-enabling the ouput */
	wlr_output_state_set_enabled(&state, event->mode);
	wlr_output_commit_state(m->wlr_output, &state);

	m->asleep = !event->mode;
	updatemons(NULL, NULL);
}

void
quit(const Arg *arg)
{
	wl_display_terminate(dpy);
}

void
refresh(const Arg *arg)
{
	Monitor *m;

	/* Force re-arrangement of all monitors */
	wl_list_for_each(m, &mons, link) {
		if (m->wlr_output->enabled) {
			arrangelayers(m);
			m->w = m->m;
			m->w.y += cell_height;
			m->w.height -= cell_height;
			updatebar(m);
			arrange(m);
		}
	}
}

void
rendermon(struct wl_listener *listener, void *data)
{
	/* This function is called every time an output is ready to display a frame,
	 * generally at the output's refresh rate (e.g. 60Hz). */
	Monitor *m = wl_container_of(listener, m, frame);
	Client *c;
	struct wlr_output_state pending = {0};
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);

	/* Render if no XDG clients have an outstanding resize and are visible on
	 * this monitor. */
	wl_list_for_each(c, &clients, link) {
		if (c->resize && !c->isfloating && client_is_rendered_on_mon(c, m) && !client_is_stopped(c))
			goto skip;
	}

	wlr_scene_output_commit(m->scene_output, NULL);

skip:
	/* Let clients know a frame has been rendered */
	wlr_scene_output_send_frame_done(m->scene_output, &now);
	wlr_output_state_finish(&pending);
}

void
requestdecorationmode(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_decoration_mode);
	if (c->surface.xdg->initialized)
		wlr_xdg_toplevel_decoration_v1_set_mode(c->decoration,
				WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void
requeststartdrag(struct wl_listener *listener, void *data)
{
	struct wlr_seat_request_start_drag_event *event = data;

	if (wlr_seat_validate_pointer_grab_serial(seat, event->origin,
			event->serial))
		wlr_seat_start_pointer_drag(seat, event->drag, event->serial);
	else
		wlr_data_source_destroy(event->drag->source);
}

void
requestmonstate(struct wl_listener *listener, void *data)
{
	struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(event->output, event->state);
	updatemons(NULL, NULL);
}

void
resize(Client *c, struct wlr_box geo, int interact)
{
	struct wlr_box *bbox;
	struct wlr_box clip;
	int frame_inset;

	if (!c->mon || !client_surface(c)->mapped)
		return;

	bbox = interact ? &sgeom : (c->isfullscreen ? &c->mon->m : &c->mon->w);

	/* Snap to grid if not fullscreen */
	/* For floating windows during interactive move (interact=1), skip position snap */
	if (!c->isfullscreen && cell_width > 0 && cell_height > 0) {
		if (!c->isfloating || !interact) {
			/* Tiled windows or non-interactive: snap position */
			geo.x = (geo.x / cell_width) * cell_width;
			geo.y = (geo.y / cell_height) * cell_height;
		}
		/* Always snap size to grid */
		geo.width = ((geo.width + cell_width - 1) / cell_width) * cell_width;
		geo.height = ((geo.height + cell_height - 1) / cell_height) * cell_height;
		/* Minimum size: 3 cells wide (border+content+border), 3 cells tall */
		if (geo.width < cell_width * 3) geo.width = cell_width * 3;
		if (geo.height < cell_height * 3) geo.height = cell_height * 3;
	}

	client_set_bounds(c, geo.width, geo.height);
	c->geom = geo;
	applybounds(c, bbox);

	/* Frame is 1 cell on each side (top has title, others are box chars) */
	frame_inset = c->isfullscreen ? 0 : cell_width;

	/* Update scene-graph */
	wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
	/* Content is inset by 1 cell on left and 1 cell on top */
	wlr_scene_node_set_position(&c->scene_surface->node, frame_inset, 
			c->isfullscreen ? 0 : cell_height);

	/* Update text frame */
	updateframe(c);

	/* Content size is window size minus frame (1 cell each side) */
	c->resize = client_set_size(c, 
			c->geom.width - (c->isfullscreen ? 0 : 2 * cell_width),
			c->geom.height - (c->isfullscreen ? 0 : 2 * cell_height));
	client_get_clip(c, &clip);
	wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);
}

void
run(char *startup_cmd)
{
	/* Add a Unix socket to the Wayland display. */
	const char *socket = wl_display_add_socket_auto(dpy);
	if (!socket)
		die("startup: display_add_socket_auto");
	if (setenv("WAYLAND_DISPLAY", socket, 1) != 0)
		die("startup: setenv WAYLAND_DISPLAY failed");
	tbwm_log(TBWM_LOG_INFO, "tbwm: WAYLAND_DISPLAY=%s DISPLAY=%s\n", socket, getenv("DISPLAY") ? getenv("DISPLAY") : "(none)");

	/* Start capturing stderr into the REPL pipe so runtime WARNING/ERROR from
	 * libraries and our own code can be forwarded into the desktop REPL.
	 * Create a non-blocking pipe and make the write end the process's stderr. */
	{
		int p[2];
		if (pipe(p) == 0) {
			repl_stderr_fd = p[0];
			repl_stderr_wfd = p[1];
			/* Make read end non-blocking */
			int flags = fcntl(repl_stderr_fd, F_GETFL, 0);
			if (flags >= 0)
				fcntl(repl_stderr_fd, F_SETFL, flags | O_NONBLOCK);
			/* Replace STDERR with write end of pipe so fprintf(stderr,..) goes there */
			if (dup2(repl_stderr_wfd, STDERR_FILENO) < 0) {
				/* fall back: keep stderr as-is */
				close(repl_stderr_fd);
				repl_stderr_fd = -1;
				close(repl_stderr_wfd);
				repl_stderr_wfd = -1;
			} else {
				/* Register event source to read from the pipe in the Wayland event loop */
				struct wl_event_loop *ev = wl_display_get_event_loop(dpy);
				repl_stderr_source = wl_event_loop_add_fd(ev, repl_stderr_fd,
					WL_EVENT_READABLE, repl_stderr_cb, NULL);
			}
		}
	}

	/* Start the backend. This will enumerate outputs and inputs, become the DRM
	 * master, etc */
	if (!wlr_backend_start(backend))
		die("startup: backend_start");

	/* Now that the socket exists and the backend is started, run the startup command */
	if (startup_cmd) {
		int piperw[2];
		if (pipe(piperw) < 0)
			die("startup: pipe:");
		if ((child_pid = fork()) < 0)
			die("startup: fork:");
		if (child_pid == 0) {
			setsid();
			dup2(piperw[0], STDIN_FILENO);
			close(piperw[0]);
			close(piperw[1]);
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, NULL);
			die("startup: execl:");
		}
		dup2(piperw[1], STDOUT_FILENO);
		close(piperw[1]);
		close(piperw[0]);
	}

	/* Mark stdout as non-blocking to avoid the startup script
	 * causing tbwm to freeze when a user neither closes stdin
	 * nor consumes standard input in his startup script */

	if (fd_set_nonblock(STDOUT_FILENO) < 0)
		close(STDOUT_FILENO);

	printstatus();

	/* At this point the outputs are initialized, choose initial selmon based on
	 * cursor position, and set default cursor image */
	selmon = xytomon(cursor->x, cursor->y);

	/* TODO hack to get cursor to display in its initial location (100, 100)
	 * instead of (0, 0) and then jumping. Still may not be fully
	 * initialized, as the image/coordinates are not transformed for the
	 * monitor when displayed here */
	wlr_cursor_warp_closest(cursor, NULL, cursor->x, cursor->y);
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

	/* Start bar update timer */
	bar_timer = wl_event_loop_add_timer(event_loop, bartimer, NULL);
	wl_event_source_timer_update(bar_timer, 1000);

	/* Start scroll timer (33ms = ~30fps for smooth scrolling) */
	scroll_timer = wl_event_loop_add_timer(event_loop, scrolltimer, NULL);
	wl_event_source_timer_update(scroll_timer, 33);

	/* Run on-startup commands from config */
	run_startup_commands();

	/* Run the Wayland event loop. This does not return until you exit the
	 * compositor. Starting the backend rigged up all of the necessary event
	 * loop configuration to listen to libinput events, DRM events, generate
	 * frame events at the refresh rate, and so on. */
	wl_display_run(dpy);
}

void
setcursor(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client provides a cursor image */
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	/* If we're "grabbing" the cursor, don't use the client's image, we will
	 * restore it after "grabbing" sending a leave event, followed by a enter
	 * event, which will result in the client requesting set the cursor surface */
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	/* This can be sent by any client, so we check to make sure this one
	 * actually has pointer focus first. If so, we can tell the cursor to
	 * use the provided surface as the cursor image. It will set the
	 * hardware cursor on the output that it's currently on and continue to
	 * do so as the cursor moves between outputs. */
	if (event->seat_client == seat->pointer_state.focused_client)
		wlr_cursor_set_surface(cursor, event->surface,
				event->hotspot_x, event->hotspot_y);
}

void
setcursorshape(struct wl_listener *listener, void *data)
{
	struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	/* This can be sent by any client, so we check to make sure this one
	 * actually has pointer focus first. If so, we can tell the cursor to
	 * use the provided cursor shape. */
	if (event->seat_client == seat->pointer_state.focused_client)
		wlr_cursor_set_xcursor(cursor, cursor_mgr,
				wlr_cursor_shape_v1_name(event->shape));
}

void
setfloating(Client *c, int floating)
{
	Client *p = client_get_parent(c);
	int was_floating = c->isfloating;
	c->isfloating = floating;
	
	/* Handle dwindle tree updates */
	if (floating && !was_floating && c->dwindle) {
		/* Becoming floating: remove from dwindle tree */
		dwindle_remove(c);
	}
	/* Note: becoming tiled handled in dwindle() arrange function */
	
	/* If in floating layout do not change the client's layer */
	if (!c->mon || !client_surface(c)->mapped || !c->mon->lt[c->mon->sellt]->arrange)
		return;
	
	/* Shrink window when becoming floating */
	if (floating && !was_floating) {
		struct wlr_box newgeom;
		int new_w, new_h;
		/* Shrink to 60% of current size, snap to grid */
		new_w = (c->geom.width * 3 / 5);
		new_h = (c->geom.height * 3 / 5);
		/* Snap to grid */
		new_w = (new_w / cell_width) * cell_width;
		new_h = (new_h / cell_height) * cell_height;
		/* Minimum size */
		if (new_w < cell_width * 20) new_w = cell_width * 20;
		if (new_h < cell_height * 10) new_h = cell_height * 10;
		/* Center in the old position */
		newgeom.x = c->geom.x + (c->geom.width - new_w) / 2;
		newgeom.y = c->geom.y + (c->geom.height - new_h) / 2;
		newgeom.width = new_w;
		newgeom.height = new_h;
		/* Snap position to grid */
		newgeom.x = (newgeom.x / cell_width) * cell_width;
		newgeom.y = (newgeom.y / cell_height) * cell_height;
		resize(c, newgeom, 1);
	}
	
	wlr_scene_node_reparent(&c->scene->node, layers[c->isfullscreen ||
			(p && p->isfullscreen) ? LyrFS
			: c->isfloating ? LyrFloat : LyrTile]);
	arrange(c->mon);
	printstatus();
}

void
setfullscreen(Client *c, int fullscreen)
{
	c->isfullscreen = fullscreen;
	if (!c->mon || !client_surface(c)->mapped)
		return;
	c->bw = fullscreen ? 0 : cfg_borderpx;
	client_set_fullscreen(c, fullscreen);
	wlr_scene_node_reparent(&c->scene->node, layers[c->isfullscreen
			? LyrFS : c->isfloating ? LyrFloat : LyrTile]);

	if (fullscreen) {
		c->prev = c->geom;
		resize(c, c->mon->m, 0);
	} else {
		/* restore previous size instead of arrange for floating windows since
		 * client positions are set by the user and cannot be recalculated */
		resize(c, c->prev, 0);
	}
	arrange(c->mon);
	printstatus();
}

void
setlayout(const Arg *arg)
{
	if (!selmon)
		return;
	if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
		selmon->sellt ^= 1;
	if (arg && arg->v)
		selmon->lt[selmon->sellt] = (Layout *)arg->v;
	strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol, LENGTH(selmon->ltsymbol));
	arrange(selmon);
	printstatus();
}

/* arg > 1.0 will set mfact absolutely */
void
setmfact(const Arg *arg)
{
	float f;

	if (!arg || !selmon || !selmon->lt[selmon->sellt]->arrange)
		return;
	f = arg->f < 1.0f ? arg->f + selmon->mfact : arg->f - 1.0f;
	if (f < 0.1 || f > 0.9)
		return;
	selmon->mfact = f;
	arrange(selmon);
}

void
setmon(Client *c, Monitor *m, uint32_t newtags)
{
	Monitor *oldmon = c->mon;

	if (oldmon == m)
		return;
	c->mon = m;
	c->prev = c->geom;

	/* Scene graph sends surface leave/enter events on move and resize */
	if (oldmon)
		arrange(oldmon);
	if (m) {
		/* Make sure window actually overlaps with the monitor */
		resize(c, c->geom, 0);
		c->tags = newtags ? newtags : m->tagset[m->seltags]; /* assign tags of target monitor */
		setfullscreen(c, c->isfullscreen); /* This will call arrange(c->mon) */
		setfloating(c, c->isfloating);
	}
	focusclient(focustop(selmon), 1);
}

void
setpsel(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client wants to set the selection,
	 * usually when the user copies something. wlroots allows compositors to
	 * ignore such requests if they so choose, but in tbwm we always honor them
	 */
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(seat, event->source, event->serial);
}

void
setsel(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client wants to set the selection,
	 * usually when the user copies something. wlroots allows compositors to
	 * ignore such requests if they so choose, but in tbwm we always honor them
	 */
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(seat, event->source, event->serial);
}

void
setup(void)
{
	int drm_fd, i, sig[] = {SIGCHLD, SIGINT, SIGTERM, SIGPIPE};
	struct sigaction sa = {.sa_flags = SA_RESTART, .sa_handler = handlesig};
	sigemptyset(&sa.sa_mask);

	for (i = 0; i < (int)LENGTH(sig); i++)
		sigaction(sig[i], &sa, NULL);

	wlr_log_init(cfg_log_level, NULL);
	setupgrid();
	buildappcache();
	setup_foot_config();

	/* Initialize s7 Scheme interpreter */
	sc = s7_init();
	if (!sc) {
		tbwm_log(TBWM_LOG_WARN, "tbwm: warning: failed to initialize s7 Scheme\n");
	} else {
		tbwm_log(TBWM_LOG_INFO, "tbwm: s7 Scheme %s initialized\n", S7_VERSION);
		setup_scheme();
	} 

	/* The Wayland display is managed by libwayland. It handles accepting
	 * clients from the Unix socket, manging Wayland globals, and so on. */
	dpy = wl_display_create();
	event_loop = wl_display_get_event_loop(dpy);

	/* Setup eventfd to safely communicate signals into the Wayland event loop */
	signal_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (signal_fd >= 0) {
		signal_fd_source = wl_event_loop_add_fd(event_loop, signal_fd, WL_EVENT_READABLE, signal_fd_cb, NULL);
	} else {
		tbwm_log(TBWM_LOG_WARN, "tbwm: warning: eventfd() failed: %s\n", strerror(errno));
	} 


	/* The backend is a wlroots feature which abstracts the underlying input and
	 * output hardware. The autocreate option will choose the most suitable
	 * backend based on the current environment, such as opening an X11 window
	 * if an X11 server is running. */
	if (!(backend = wlr_backend_autocreate(event_loop, &session)))
		die("couldn't create backend");

	/* Initialize the scene graph used to lay out windows */
	scene = wlr_scene_create();
	root_bg = wlr_scene_rect_create(&scene->tree, 0, 0, cfg_rootcolor);
	for (i = 0; i < NUM_LAYERS; i++)
		layers[i] = wlr_scene_tree_create(&scene->tree);
	drag_icon = wlr_scene_tree_create(&scene->tree);
	wlr_scene_node_place_below(&drag_icon->node, &layers[LyrBlock]->node);

	/* Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The user
	 * can also specify a renderer using the WLR_RENDERER env var.
	 * The renderer is responsible for defining the various pixel formats it
	 * supports for shared memory, this configures that for clients. */
	if (!(drw = wlr_renderer_autocreate(backend)))
		die("couldn't create renderer");
	wl_signal_add(&drw->events.lost, &gpu_reset);

	/* Create shm, drm and linux_dmabuf interfaces by ourselves.
	 * The simplest way is to call:
	 *      wlr_renderer_init_wl_display(drw);
	 * but we need to create the linux_dmabuf interface manually to integrate it
	 * with wlr_scene. */
	wlr_renderer_init_wl_shm(drw, dpy);

	if (wlr_renderer_get_texture_formats(drw, WLR_BUFFER_CAP_DMABUF)) {
		wlr_drm_create(dpy, drw);
		wlr_scene_set_linux_dmabuf_v1(scene,
				wlr_linux_dmabuf_v1_create_with_renderer(dpy, 5, drw));
	}

	if ((drm_fd = wlr_renderer_get_drm_fd(drw)) >= 0 && drw->features.timeline
			&& backend->features.timeline)
		wlr_linux_drm_syncobj_manager_v1_create(dpy, 1, drm_fd);

	/* Autocreates an allocator for us.
	 * The allocator is the bridge between the renderer and the backend. It
	 * handles the buffer creation, allowing wlroots to render onto the
	 * screen */
	if (!(alloc = wlr_allocator_autocreate(backend, drw)))
		die("couldn't create allocator");

	/* This creates some hands-off wlroots interfaces. The compositor is
	 * necessary for clients to allocate surfaces and the data device manager
	 * handles the clipboard. Each of these wlroots interfaces has room for you
	 * to dig your fingers in and play with their behavior if you want. Note that
	 * the clients cannot set the selection directly without compositor approval,
	 * see the setsel() function. */
	compositor = wlr_compositor_create(dpy, 6, drw);
	wlr_subcompositor_create(dpy);
	wlr_data_device_manager_create(dpy);
	wlr_export_dmabuf_manager_v1_create(dpy);
	wlr_screencopy_manager_v1_create(dpy);
	wlr_data_control_manager_v1_create(dpy);
	wlr_primary_selection_v1_device_manager_create(dpy);
	wlr_viewporter_create(dpy);
	wlr_single_pixel_buffer_manager_v1_create(dpy);
	wlr_fractional_scale_manager_v1_create(dpy, 1);
	wlr_presentation_create(dpy, backend, 2);
	wlr_alpha_modifier_v1_create(dpy);

	/* Initializes the interface used to implement urgency hints */
	activation = wlr_xdg_activation_v1_create(dpy);
	wl_signal_add(&activation->events.request_activate, &request_activate);

	wlr_scene_set_gamma_control_manager_v1(scene, wlr_gamma_control_manager_v1_create(dpy));

	power_mgr = wlr_output_power_manager_v1_create(dpy);
	wl_signal_add(&power_mgr->events.set_mode, &output_power_mgr_set_mode);

	/* Creates an output layout, which is a wlroots utility for working with an
	 * arrangement of screens in a physical layout. */
	output_layout = wlr_output_layout_create(dpy);
	wl_signal_add(&output_layout->events.change, &layout_change);

    wlr_xdg_output_manager_v1_create(dpy, output_layout);

	/* Configure a listener to be notified when new outputs are available on the
	 * backend. */
	wl_list_init(&mons);
	wl_signal_add(&backend->events.new_output, &new_output);

	/* Set up our client lists, the xdg-shell and the layer-shell. The xdg-shell is a
	 * Wayland protocol which is used for application windows. For more
	 * detail on shells, refer to the article:
	 *
	 * https://drewdevault.com/2018/07/29/Wayland-shells.html
	 */
	wl_list_init(&clients);
	wl_list_init(&fstack);

	/* Initialize REPL and load config (must be after layers and mons init) */
	if (sc) {
		repl_add_line(";;; Welcome to TurboWM!");
		repl_add_line(";;; Run (help) for commands");
		repl_add_line(";;; Press Super+Shift+; to toggle, Escape to close");
		repl_add_line("");
		load_config();
	}

	xdg_shell = wlr_xdg_shell_create(dpy, 6);
	wl_signal_add(&xdg_shell->events.new_toplevel, &new_xdg_toplevel);
	wl_signal_add(&xdg_shell->events.new_popup, &new_xdg_popup);

	layer_shell = wlr_layer_shell_v1_create(dpy, 3);
	wl_signal_add(&layer_shell->events.new_surface, &new_layer_surface);

	idle_notifier = wlr_idle_notifier_v1_create(dpy);

	idle_inhibit_mgr = wlr_idle_inhibit_v1_create(dpy);
	wl_signal_add(&idle_inhibit_mgr->events.new_inhibitor, &new_idle_inhibitor);

	session_lock_mgr = wlr_session_lock_manager_v1_create(dpy);
	wl_signal_add(&session_lock_mgr->events.new_lock, &new_session_lock);
	locked_bg = wlr_scene_rect_create(layers[LyrBlock], sgeom.width, sgeom.height,
			(float [4]){0.1f, 0.1f, 0.1f, 1.0f});
	wlr_scene_node_set_enabled(&locked_bg->node, 0);

	/* Use decoration protocols to negotiate server-side decorations */
	wlr_server_decoration_manager_set_default_mode(
			wlr_server_decoration_manager_create(dpy),
			WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
	xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(dpy);
	wl_signal_add(&xdg_decoration_mgr->events.new_toplevel_decoration, &new_xdg_decoration);

	pointer_constraints = wlr_pointer_constraints_v1_create(dpy);
	wl_signal_add(&pointer_constraints->events.new_constraint, &new_pointer_constraint);

	relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(dpy);

	/*
	 * Creates a cursor, which is a wlroots utility for tracking the cursor
	 * image shown on screen.
	 */
	cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(cursor, output_layout);

	/* Creates an xcursor manager, another wlroots utility which loads up
	 * Xcursor themes to source cursor images from and makes sure that cursor
	 * images are available at all scale factors on the screen (necessary for
	 * HiDPI support). Scaled cursors will be loaded with each output. */
	cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
	setenv("XCURSOR_SIZE", "24", 1);

	/*
	 * wlr_cursor *only* displays an image on screen. It does not move around
	 * when the pointer moves. However, we can attach input devices to it, and
	 * it will generate aggregate events for all of them. In these events, we
	 * can choose how we want to process them, forwarding them to clients and
	 * moving the cursor around. More detail on this process is described in
	 * https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html
	 *
	 * And more comments are sprinkled throughout the notify functions above.
	 */
	wl_signal_add(&cursor->events.motion, &cursor_motion);
	wl_signal_add(&cursor->events.motion_absolute, &cursor_motion_absolute);
	wl_signal_add(&cursor->events.button, &cursor_button);
	wl_signal_add(&cursor->events.axis, &cursor_axis);
	wl_signal_add(&cursor->events.frame, &cursor_frame);

	cursor_shape_mgr = wlr_cursor_shape_manager_v1_create(dpy, 1);
	wl_signal_add(&cursor_shape_mgr->events.request_set_shape, &request_set_cursor_shape);

	/*
	 * Configures a seat, which is a single "seat" at which a user sits and
	 * operates the computer. This conceptually includes up to one keyboard,
	 * pointer, touch, and drawing tablet device. We also rig up a listener to
	 * let us know when new input devices are available on the backend.
	 */
	wl_signal_add(&backend->events.new_input, &new_input_device);
	virtual_keyboard_mgr = wlr_virtual_keyboard_manager_v1_create(dpy);
	wl_signal_add(&virtual_keyboard_mgr->events.new_virtual_keyboard,
			&new_virtual_keyboard);
	virtual_pointer_mgr = wlr_virtual_pointer_manager_v1_create(dpy);
    wl_signal_add(&virtual_pointer_mgr->events.new_virtual_pointer,
            &new_virtual_pointer);

	seat = wlr_seat_create(dpy, "seat0");
	wl_signal_add(&seat->events.request_set_cursor, &request_cursor);
	wl_signal_add(&seat->events.request_set_selection, &request_set_sel);
	wl_signal_add(&seat->events.request_set_primary_selection, &request_set_psel);
	wl_signal_add(&seat->events.request_start_drag, &request_start_drag);
	wl_signal_add(&seat->events.start_drag, &start_drag);

	kb_group = createkeyboardgroup();
	wl_list_init(&kb_group->destroy.link);

	output_mgr = wlr_output_manager_v1_create(dpy);
	wl_signal_add(&output_mgr->events.apply, &output_mgr_apply);
	wl_signal_add(&output_mgr->events.test, &output_mgr_test);

	/* Make sure XWayland clients don't connect to the parent X server,
	 * e.g when running in the x11 backend or the wayland backend and the
	 * compositor has Xwayland support */
	unsetenv("DISPLAY");
#ifdef XWAYLAND
	/*
	 * Initialise the XWayland X server.
	 * It will be started when the first X client is started.
	 */
	{
		/* Check if Xwayland binary exists */
		const char *xwayland_paths[] = {
			"/usr/bin/Xwayland",
			"/usr/local/bin/Xwayland",
			"/bin/Xwayland",
			NULL
		};
		int xwayland_found = 0;
		for (int i = 0; xwayland_paths[i]; i++) {
			if (access(xwayland_paths[i], X_OK) == 0) {
				xwayland_found = 1;
				tbwm_log(TBWM_LOG_INFO, "tbwm: Found Xwayland at %s\n", xwayland_paths[i]);
				break;
			}
		}
		if (!xwayland_found) {
			tbwm_log(TBWM_LOG_WARN, "tbwm: WARNING: Xwayland binary not found in standard paths\n");
			tbwm_log(TBWM_LOG_WARN, "tbwm: Install xorg-xwayland package for X11 app support\n");
		}
	}
	
	if ((xwayland = wlr_xwayland_create(dpy, compositor, 1))) {
		wl_signal_add(&xwayland->events.ready, &xwayland_ready);
		wl_signal_add(&xwayland->events.new_surface, &new_xwayland_surface);

		setenv("DISPLAY", xwayland->display_name, 1);
		tbwm_log(TBWM_LOG_INFO, "tbwm: XWayland started on DISPLAY=%s\n", xwayland->display_name);
	} else {
		tbwm_log(TBWM_LOG_ERROR, "tbwm: ERROR: failed to setup XWayland X server, continuing without it\n");
		tbwm_log(TBWM_LOG_ERROR, "tbwm: Check: 1) xorg-xwayland installed  2) /tmp/.X11-unix writable  3) no stale X locks in /tmp\n");
	} 
#endif
}

void
spawn(const Arg *arg)
{
	tbwm_log(TBWM_LOG_INFO, "tbwm: spawning %s (DISPLAY=%s WAYLAND_DISPLAY=%s)\n",
		((char **)arg->v)[0],
		getenv("DISPLAY") ? getenv("DISPLAY") : "(none)",
		getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "(none)");
	if (fork() == 0) {
		dup2(STDERR_FILENO, STDOUT_FILENO);
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		die("tbwm: execvp %s failed:", ((char **)arg->v)[0]);
	}
}

void
setupgrid(void)
{
	FT_Error err;

	err = FT_Init_FreeType(&ft_library);
	if (err) {
		tbwm_log(TBWM_LOG_ERROR, "Failed to init FreeType");
		return;
	}

	err = FT_New_Face(ft_library, cfg_font_path, 0, &ft_face);
	if (err) {
		tbwm_log(TBWM_LOG_ERROR, "Failed to load font: %s", cfg_font_path);
		return;
	}

	FT_Set_Pixel_Sizes(ft_face, 0, cfg_font_size);

	/* Get cell dimensions from font metrics */
	if (FT_Load_Char(ft_face, 'M', FT_LOAD_DEFAULT) == 0) {
		cell_width = ft_face->glyph->advance.x >> 6;
		cell_height = ft_face->size->metrics.height >> 6;
	}

	/* Informational only: writes to debug file */
	tbwm_log(TBWM_LOG_INFO, "Grid: %dx%d cells (font: %s)", cell_width, cell_height, cfg_font_path);
}

/* ==================== SCHEME BINDINGS ==================== */

/* Scheme function: (spawn cmd) - launch a program */
static s7_pointer scm_spawn(s7_scheme *sc, s7_pointer args)
{
	const char *cmd;
	if (!s7_is_string(s7_car(args)))
		return s7_f(sc);
	cmd = s7_string(s7_car(args));
	if (fork() == 0) {
		setsid();
		execlp("/bin/sh", "/bin/sh", "-c", cmd, NULL);
		_exit(EXIT_FAILURE);
	}
	return s7_t(sc);
}

/* Scheme function: (quit) - exit the WM */
static s7_pointer scm_quit(s7_scheme *sc, s7_pointer args)
{
	wl_display_terminate(dpy);
	return s7_t(sc);
}

/* Scheme function: (focus-dir direction) - focus window in direction (0=left,1=right,2=up,3=down) */
static s7_pointer scm_focus_dir(s7_scheme *sc, s7_pointer args)
{
	Arg arg;
	if (!s7_is_integer(s7_car(args)))
		return s7_f(sc);
	arg.i = s7_integer(s7_car(args));
	focusdir(&arg);
	return s7_t(sc);
}

/* Scheme function: (swap-dir direction) - swap window in direction */
static s7_pointer scm_swap_dir(s7_scheme *sc, s7_pointer args)
{
	Arg arg;
	if (!s7_is_integer(s7_car(args)))
		return s7_f(sc);
	arg.i = s7_integer(s7_car(args));
	swapdir(&arg);
	return s7_t(sc);
}

/* Scheme function: (view-tag n) - switch to tag n (1-9) */
static s7_pointer scm_view_tag(s7_scheme *sc, s7_pointer args)
{
	Arg arg;
	int n;
	if (!s7_is_integer(s7_car(args)))
		return s7_f(sc);
	n = s7_integer(s7_car(args));
	if (n < 1 || n > 9)
		return s7_f(sc);
	arg.ui = 1 << (n - 1);
	view(&arg);
	return s7_t(sc);
}

/* Scheme function: (tag-window n) - move focused window to tag n */
static s7_pointer scm_tag_window(s7_scheme *sc, s7_pointer args)
{
	Arg arg;
	int n;
	if (!s7_is_integer(s7_car(args)))
		return s7_f(sc);
	n = s7_integer(s7_car(args));
	if (n < 1 || n > 9)
		return s7_f(sc);
	arg.ui = 1 << (n - 1);
	tag(&arg);
	return s7_t(sc);
}

/* Scheme function: (toggle-floating) - toggle floating for focused window */
static s7_pointer scm_toggle_floating(s7_scheme *sc, s7_pointer args)
{
	togglefloating(NULL);
	return s7_t(sc);
}

/* Scheme function: (toggle-fullscreen) - toggle fullscreen for focused window */
static s7_pointer scm_toggle_fullscreen(s7_scheme *sc, s7_pointer args)
{
	togglefullscreen(NULL);
	return s7_t(sc);
}

/* Scheme function: (kill-client) - close focused window */
static s7_pointer scm_kill_client(s7_scheme *sc, s7_pointer args)
{
	killclient(NULL);
	return s7_t(sc);
}

/* Scheme function: (refresh) - refresh layout */
static s7_pointer scm_refresh(s7_scheme *sc, s7_pointer args)
{
	refresh(NULL);
	return s7_t(sc);
}

/* Scheme function: (toggle-launcher) - open/close launcher */
static s7_pointer scm_toggle_launcher(s7_scheme *sc, s7_pointer args)
{
	togglelauncher(NULL);
	return s7_t(sc);
}

/* Scheme function: (focus-monitor dir) - focus monitor in direction */
static s7_pointer scm_focus_monitor(s7_scheme *sc, s7_pointer args)
{
	Arg arg;
	if (!s7_is_integer(s7_car(args)))
		return s7_f(sc);
	arg.i = s7_integer(s7_car(args));
	focusmon(&arg);
	return s7_t(sc);
}

/* Scheme function: (tag-monitor dir) - send window to monitor in direction */
static s7_pointer scm_tag_monitor(s7_scheme *sc, s7_pointer args)
{
	Arg arg;
	if (!s7_is_integer(s7_car(args)))
		return s7_f(sc);
	arg.i = s7_integer(s7_car(args));
	tagmon(&arg);
	return s7_t(sc);
}

/* Scheme function: (set-layout name) - set layout by name: "tile", "dwindle", "monocle", "float" */
static s7_pointer scm_set_layout(s7_scheme *sc, s7_pointer args)
{
	const char *name;
	Arg arg;
	if (!s7_is_string(s7_car(args)))
		return s7_f(sc);
	name = s7_string(s7_car(args));
	
	if (strcmp(name, "tile") == 0)
		arg.v = &layouts[1];
	else if (strcmp(name, "dwindle") == 0)
		arg.v = &layouts[0];
	else if (strcmp(name, "monocle") == 0)
		arg.v = &layouts[3];
	else if (strcmp(name, "float") == 0)
		arg.v = &layouts[2];
	else
		return s7_f(sc);
	
	setlayout(&arg);
	return s7_t(sc);
}

/* Scheme function: (cycle-layout) - cycle through layouts */
static s7_pointer scm_cycle_layout(s7_scheme *sc, s7_pointer args)
{
	setlayout(NULL);
	return s7_t(sc);
}

/* Scheme function: (inc-mfact delta) - increase/decrease master factor */
static s7_pointer scm_inc_mfact(s7_scheme *sc, s7_pointer args)
{
	Arg arg;
	if (!s7_is_real(s7_car(args)))
		return s7_f(sc);
	arg.f = s7_real(s7_car(args));
	setmfact(&arg);
	return s7_t(sc);
}

/* Scheme function: (inc-nmaster delta) - increase/decrease number of masters */
static s7_pointer scm_inc_nmaster(s7_scheme *sc, s7_pointer args)
{
	Arg arg;
	if (!s7_is_integer(s7_car(args)))
		return s7_f(sc);
	arg.i = s7_integer(s7_car(args));
	incnmaster(&arg);
	return s7_t(sc);
}

/* Scheme function: (toggle-tag n) - toggle tag visibility */
static s7_pointer scm_toggle_tag(s7_scheme *sc, s7_pointer args)
{
	Arg arg;
	int n;
	if (!s7_is_integer(s7_car(args)))
		return s7_f(sc);
	n = s7_integer(s7_car(args));
	if (n < 1 || n > 9)
		return s7_f(sc);
	arg.ui = 1 << (n - 1);
	toggleview(&arg);
	return s7_t(sc);
}

/* Scheme function: (toggle-window-tag n) - toggle tag on focused window */
static s7_pointer scm_toggle_window_tag(s7_scheme *sc, s7_pointer args)
{
	Arg arg;
	int n;
	if (!s7_is_integer(s7_car(args)))
		return s7_f(sc);
	n = s7_integer(s7_car(args));
	if (n < 1 || n > 9)
		return s7_f(sc);
	arg.ui = 1 << (n - 1);
	toggletag(&arg);
	return s7_t(sc);
}

/* Scheme function: (toggle-repl) - toggle REPL input mode */
static s7_pointer scm_toggle_repl(s7_scheme *sc, s7_pointer args)
{
	togglerepl(NULL);
	return s7_t(sc);
}

/* Scheme: (move-window) - start moving focused window with mouse */
static s7_pointer scm_move_window(s7_scheme *sc, s7_pointer args)
{
	Arg arg = {.ui = CurMove};
	moveresize(&arg);
	return s7_t(sc);
}

/* Scheme: (resize-window) - start resizing focused window with mouse */
static s7_pointer scm_resize_window(s7_scheme *sc, s7_pointer args)
{
	Arg arg = {.ui = CurResize};
	moveresize(&arg);
	return s7_t(sc);
}

/* Scheme function: (zoom) - swap focused window with master */
static s7_pointer scm_zoom(s7_scheme *sc, s7_pointer args)
{
	zoom(NULL);
	return s7_t(sc);
}

/* Scheme function: (focus-stack delta) - focus next/prev in stack */
static s7_pointer scm_focus_stack(s7_scheme *sc, s7_pointer args)
{
	Arg arg;
	if (!s7_is_integer(s7_car(args)))
		return s7_f(sc);
	arg.i = s7_integer(s7_car(args));
	focusstack(&arg);
	return s7_t(sc);
}

/* Scheme function: (view-all) - view all tags */
static s7_pointer scm_view_all(s7_scheme *sc, s7_pointer args)
{
	Arg arg;
	arg.ui = ~0;
	view(&arg);
	return s7_t(sc);
}

/* Scheme function: (tag-all) - set window to all tags */
static s7_pointer scm_tag_all(s7_scheme *sc, s7_pointer args)
{
	Arg arg;
	arg.ui = ~0;
	tag(&arg);
	return s7_t(sc);
}

/* Scheme function: (eval-string str) - evaluate Scheme code string */
static s7_pointer scm_eval_string(s7_scheme *sc, s7_pointer args)
{
	if (!s7_is_string(s7_car(args)))
		return s7_f(sc);
	return s7_eval_c_string(sc, s7_string(s7_car(args)));
}

/* Scheme function: (reload-config) - reload config file */
static s7_pointer scm_reload_config(s7_scheme *sc, s7_pointer args)
{
	load_config();
	return s7_t(sc);
}

/* Scheme function: (focused-app-id) - get app_id of focused window */
static s7_pointer scm_focused_app_id(s7_scheme *sc, s7_pointer args)
{
	Client *c;
	if (!selmon)
		return s7_f(sc);
	c = focustop(selmon);
	if (!c)
		return s7_f(sc);
	return s7_make_string(sc, client_get_appid(c));
}

/* Scheme function: (focused-title) - get title of focused window */
static s7_pointer scm_focused_title(s7_scheme *sc, s7_pointer args)
{
	Client *c;
	if (!selmon)
		return s7_f(sc);
	c = focustop(selmon);
	if (!c)
		return s7_f(sc);
	return s7_make_string(sc, client_get_title(c));
}

/* Scheme function: (current-tag) - get current tag number */
static s7_pointer scm_current_tag(s7_scheme *sc, s7_pointer args)
{
	int i;
	uint32_t tags;
	if (!selmon)
		return s7_make_integer(sc, 1);
	tags = selmon->tagset[selmon->seltags];
	for (i = 0; i < 9; i++) {
		if (tags & (1 << i))
			return s7_make_integer(sc, i + 1);
	}
	return s7_make_integer(sc, 1);
}

/* Scheme function: (window-count) - get number of visible windows */
static s7_pointer scm_window_count(s7_scheme *sc, s7_pointer args)
{
	Client *c;
	int count = 0;
	if (!selmon)
		return s7_make_integer(sc, 0);
	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, selmon))
			count++;
	}
	return s7_make_integer(sc, count);
}

/* Scheme function: (log msg) - print message to stderr */
static s7_pointer scm_log(s7_scheme *sc, s7_pointer args)
{
	if (s7_is_string(s7_car(args)))
		tbwm_log(TBWM_LOG_WARN, "tbwm-scm: %s\n", s7_string(s7_car(args)));
	return s7_t(sc);
}

/* Scheme function: (help) - show available commands */
static s7_pointer scm_help(s7_scheme *sc, s7_pointer args)
{
	repl_add_line("=== TurboWM Commands ===");
	repl_add_line("(spawn \"name\")     - run a program");
	repl_add_line("(quit)             - exit TurboWM");
	repl_add_line("(kill-client)      - close focused window");
	repl_add_line("(toggle-floating)  - toggle floating mode");
	repl_add_line("(toggle-fullscreen)- toggle fullscreen");
	repl_add_line("(focus-dir DIR)    - focus window in direction");
	repl_add_line("(swap-dir DIR)     - swap window in direction");
	repl_add_line("(view-tag N)       - switch to tag N");
	repl_add_line("(tag-window N)     - move window to tag N");
	repl_add_line("(reload-config)    - reload config.scm");
	repl_add_line("(bind-key K F)     - bind key to function");
	repl_add_line("=== Appearance ===");
	repl_add_line("(set-background-color C) - highlight/bg color");
	repl_add_line("(set-border-line-color C)- box-drawing color");
	repl_add_line("(set-tag-count N)  - virtual desktops (1-9)");
	repl_add_line("(set-show-time B)  - show time in bar");
	repl_add_line("(set-show-date B)  - show date in bar");
	repl_add_line("(set-status-text S)- custom status text");
	repl_add_line("DIR: DIR-LEFT/RIGHT/UP/DOWN");
	return s7_t(sc);
}

/* Scheme function: (chvt n) - switch to virtual terminal n */
static s7_pointer scm_chvt(s7_scheme *sc, s7_pointer args)
{
	Arg arg;
	if (!s7_is_integer(s7_car(args)))
		return s7_f(sc);
	arg.ui = s7_integer(s7_car(args));
	/* Log the scheme request (INFO level) so debug file shows it; errors are promoted to REPL */
	tbwm_log(TBWM_LOG_INFO, "scm_chvt: request to change VT to %u\n", arg.ui);
	chvt(&arg);
	return s7_t(sc);
}

/* Scheme function: (set-repl-log-level n) - set REPL log threshold (0=DEBUG,1=INFO,2=WARN,3=ERROR) */
static s7_pointer scm_set_repl_log_level(s7_scheme *sc, s7_pointer args)
{
	if (!s7_is_integer(s7_car(args)))
		return s7_f(sc);
	cfg_repl_log_level = s7_integer(s7_car(args));
	tbwm_log(TBWM_LOG_INFO, "set-repl-log-level: set to %d\n", cfg_repl_log_level);
	return s7_t(sc);
}

/* Scheme function: (set-title-scroll-mode mode) - set scroll mode (0=truncate, 1=scroll) */
static s7_pointer scm_set_title_scroll_mode(s7_scheme *sc, s7_pointer args)
{
	if (!s7_is_integer(s7_car(args)))
		return s7_f(sc);
	title_scroll_mode = s7_integer(s7_car(args)) ? 1 : 0;
	return s7_t(sc);
}

/* Scheme function: (set-title-scroll-speed speed) - set scroll speed (pixels per tick) */
static s7_pointer scm_set_title_scroll_speed(s7_scheme *sc, s7_pointer args)
{
	if (!s7_is_integer(s7_car(args)))
		return s7_f(sc);
	title_scroll_speed = s7_integer(s7_car(args));
	if (title_scroll_speed < 1) title_scroll_speed = 1;
	return s7_t(sc);
}

/* Storage for Scheme keybindings (dynamic) */
typedef struct {
	uint32_t mod;
	xkb_keysym_t keysym;
	s7_pointer callback;
	s7_int gc_loc;
} SchemeBinding;
static SchemeBinding *scheme_bindings = NULL;
static int scheme_binding_count = 0;
static int scheme_binding_capacity = 0;

static void
ensure_scheme_bindings_capacity(int extra)
{
	if (scheme_binding_capacity - scheme_binding_count >= extra)
		return;
	int newcap = scheme_binding_capacity ? scheme_binding_capacity * 2 : 16;
	while (newcap - scheme_binding_count < extra)
		newcap *= 2;
	SchemeBinding *tmp = realloc(scheme_bindings, newcap * sizeof(SchemeBinding));
	if (!tmp) {
		tbwm_log(TBWM_LOG_ERROR, "tbwm: out of memory growing scheme bindings\n");
		return;
	}
	scheme_bindings = tmp;
	for (int i = scheme_binding_capacity; i < newcap; ++i) {
		scheme_bindings[i].callback = s7_nil(sc);
		scheme_bindings[i].gc_loc = -1;
	}
	scheme_binding_capacity = newcap;
	file_debug_log("tbwm-scm: scheme_bindings capacity grown to %d\n", scheme_binding_capacity);
}

/* Parse modifier string like "M-S-" to modifier mask */
static uint32_t parse_modifiers(const char *str, const char **rest)
{
	uint32_t mods = 0;
	while (*str && *(str+1) == '-') {
		switch (*str) {
		case 'M': mods |= WLR_MODIFIER_LOGO; break;   /* Super/Meta */
		case 'S': mods |= WLR_MODIFIER_SHIFT; break;  /* Shift */
		case 'C': mods |= WLR_MODIFIER_CTRL; break;   /* Control */
		case 'A': mods |= WLR_MODIFIER_ALT; break;    /* Alt */
		}
		str += 2;
	}
	*rest = str;
	return mods;
}

/* Scheme function: (bind-key "M-Return" callback) - bind a key to a Scheme function */
static s7_pointer scm_bind_key(s7_scheme *sc, s7_pointer args)
{
	const char *keystr, *keyname;
	s7_pointer callback;
	uint32_t mods;
	xkb_keysym_t sym;

	if (!s7_is_string(s7_car(args)))
		return s7_f(sc);
	keystr = s7_string(s7_car(args));
	callback = s7_cadr(args);

	if (!s7_is_procedure(callback))
		return s7_f(sc);

	mods = parse_modifiers(keystr, &keyname);
	sym = xkb_keysym_from_name(keyname, XKB_KEYSYM_CASE_INSENSITIVE);
	if (sym == XKB_KEY_NoSymbol) {
		tbwm_log(TBWM_LOG_ERROR, "tbwm-scm: unknown key: %s", keyname);
		return s7_f(sc);
	}

	/* If an identical binding exists, replace it (and unprotect the old one). */
	for (int i = 0; i < scheme_binding_count; i++) {
		if (CLEANMASK(scheme_bindings[i].mod) == CLEANMASK(mods) && scheme_bindings[i].keysym == sym) {
			/* Replace existing binding */
			if (scheme_bindings[i].gc_loc >= 0)
				s7_gc_unprotect_at(sc, scheme_bindings[i].gc_loc);
			s7_int gc_loc = s7_gc_protect(sc, callback);
			scheme_bindings[i].callback = callback;
			scheme_bindings[i].gc_loc = gc_loc;
			file_debug_log("tbwm-scm: replaced binding %s (idx=%d)\n", keystr, i);
			return s7_t(sc);
		}
	}

	/* Ensure capacity and append new binding */
	ensure_scheme_bindings_capacity(1);

	/* Protect callback from GC - store protection location so we can unprotect on reload */
	s7_int gc_loc = s7_gc_protect(sc, callback);

	scheme_bindings[scheme_binding_count].mod = mods;
	scheme_bindings[scheme_binding_count].keysym = sym;
	scheme_bindings[scheme_binding_count].callback = callback;
	scheme_bindings[scheme_binding_count].gc_loc = gc_loc;
	scheme_binding_count++;

	/* Debug-only: do not spam the REPL with routine bindings */
	file_debug_log("tbwm-scm: bound %s (mod=0x%x, sym=0x%x)\n", keystr, mods, sym);
	return s7_t(sc);
}

/* Internal: unprotect and remove all Scheme keybindings */
static void
unbind_all_scheme_bindings(void)
{
	int i;
	for (i = 0; i < scheme_binding_count; i++) {
		if (scheme_bindings[i].gc_loc >= 0)
			s7_gc_unprotect_at(sc, scheme_bindings[i].gc_loc);
		scheme_bindings[i].callback = s7_nil(sc);
		scheme_bindings[i].gc_loc = -1;
	}
	scheme_binding_count = 0;
}

/* Scheme function: (unbind-all) - remove all Scheme keybindings */
static s7_pointer scm_unbind_all(s7_scheme *sc, s7_pointer args)
{
	unbind_all_scheme_bindings();
	return s7_t(sc);
}

/* Check and execute Scheme keybindings - returns 1 if handled */
int check_scheme_bindings(uint32_t mods, xkb_keysym_t sym)
{
	int i;
	xkb_keysym_t sym_lower = xkb_keysym_to_lower(sym);
	char symname[64] = {0};
	xkb_keysym_get_name(sym, symname, sizeof(symname));
	file_debug_log("tbwm: check_scheme_bindings: checking %d bindings for mods=0x%x sym=0x%x (%s)\n",
				scheme_binding_count, mods, sym, symname[0] ? symname : "(no-name)");
	int best_idx = -1;
	int best_spec = -1; /* number of modifier bits in binding (higher == more specific) */
	for (i = 0; i < scheme_binding_count; i++) {
		xkb_keysym_t bound_lower = xkb_keysym_to_lower(scheme_bindings[i].keysym);
		char boundname[64] = {0};
		xkb_keysym_get_name(scheme_bindings[i].keysym, boundname, sizeof(boundname));
		file_debug_log("tbwm: scheme idx=%d stored mod=0x%x sym=0x%x (%s)\n", i, scheme_bindings[i].mod, scheme_bindings[i].keysym, boundname[0] ? boundname : "(no-name)");
		/* Collect best (most-specific) matching binding so that M-S-Left wins over M-Left when both exist. */
		if ((CLEANMASK(mods) & CLEANMASK(scheme_bindings[i].mod)) == CLEANMASK(scheme_bindings[i].mod) && 
		    (scheme_bindings[i].keysym == sym || bound_lower == sym_lower)) {
			int spec = __builtin_popcount(CLEANMASK(scheme_bindings[i].mod));
			if (spec > best_spec) {
				best_spec = spec;
				best_idx = i;
			}
		}
	}
	if (best_idx >= 0) {
		tbwm_log(TBWM_LOG_INFO, "tbwm: scheme binding matched (idx=%d) mods=0x%x sym=0x%x (best_spec=%d)\n", best_idx, mods, sym, best_spec);
		s7_call(sc, scheme_bindings[best_idx].callback, s7_nil(sc));
		return 1;
	}
	return 0;
}

/* ==================== SCHEME CONFIG SETTERS ==================== */

/* Helper: parse hex color #RRGGBB to uint32_t (no alpha) */
static uint32_t parse_color_rgb(const char *str) {
	unsigned int r = 0, g = 0, b = 0;
	if (str[0] == '#') str++;
	if (strlen(str) >= 6) {
		sscanf(str, "%02x%02x%02x", &r, &g, &b);
	}
	return (r << 16) | (g << 8) | b;
}

/* Helper: get ARGB from RGB (add full alpha) */
#define RGB_TO_ARGB(rgb) (0xFF000000 | (rgb))

/* Scheme: (set-sloppy-focus b) */
static s7_pointer scm_set_sloppy_focus(s7_scheme *sc, s7_pointer args) {
	cfg_sloppyfocus = s7_boolean(sc, s7_car(args)) ? 1 : 0;
	return s7_t(sc);
}

/* ========== COLOR CONFIG API ========== */

/* Scheme: (set-bg-color "#RRGGBB") - root/REPL background */
static s7_pointer scm_set_bg_color(s7_scheme *sc, s7_pointer args) {
	if (!s7_is_string(s7_car(args))) return s7_f(sc);
	cfg_bg_color = parse_color_rgb(s7_string(s7_car(args)));
	/* Update root background if it exists */
	if (root_bg) {
		float c[4];
		c[0] = ((cfg_bg_color >> 16) & 0xFF) / 255.0f;
		c[1] = ((cfg_bg_color >> 8) & 0xFF) / 255.0f;
		c[2] = (cfg_bg_color & 0xFF) / 255.0f;
		c[3] = 1.0f;
		wlr_scene_rect_set_color(root_bg, c);
	}
	updatebars();
	return s7_t(sc);
}

/* Scheme: (set-bg-text-color "#RRGGBB") - text on background/REPL */
static s7_pointer scm_set_bg_text_color(s7_scheme *sc, s7_pointer args) {
	if (!s7_is_string(s7_car(args))) return s7_f(sc);
	cfg_bg_text_color = parse_color_rgb(s7_string(s7_car(args)));
	updatebars();
	return s7_t(sc);
}

/* Scheme: (set-border-color "#RRGGBB") - border highlight (blue) */
static s7_pointer scm_set_border_color(s7_scheme *sc, s7_pointer args) {
	if (!s7_is_string(s7_car(args))) return s7_f(sc);
	cfg_border_color = parse_color_rgb(s7_string(s7_car(args)));
	updateframes();
	updatebars();
	return s7_t(sc);
}

/* Scheme: (set-border-line-color "#RRGGBB") - box drawing chars (grey) */
static s7_pointer scm_set_border_line_color(s7_scheme *sc, s7_pointer args) {
	if (!s7_is_string(s7_car(args))) return s7_f(sc);
	cfg_border_line_color = parse_color_rgb(s7_string(s7_car(args)));
	updateframes();
	updatebars();
	return s7_t(sc);
}

/* Scheme: (set-bar-color "#RRGGBB") - status bar background */
static s7_pointer scm_set_bar_color(s7_scheme *sc, s7_pointer args) {
	if (!s7_is_string(s7_car(args))) return s7_f(sc);
	cfg_bar_color = parse_color_rgb(s7_string(s7_car(args)));
	updatebars();
	return s7_t(sc);
}

/* Scheme: (set-bar-text-color "#RRGGBB") - status bar text */
static s7_pointer scm_set_bar_text_color(s7_scheme *sc, s7_pointer args) {
	if (!s7_is_string(s7_car(args))) return s7_f(sc);
	cfg_bar_text_color = parse_color_rgb(s7_string(s7_car(args)));
	updatebars();
	return s7_t(sc);
}

/* Scheme: (set-menu-color "#RRGGBB") - app menu background */
static s7_pointer scm_set_menu_color(s7_scheme *sc, s7_pointer args) {
	if (!s7_is_string(s7_car(args))) return s7_f(sc);
	cfg_menu_color = parse_color_rgb(s7_string(s7_car(args)));
	updateappmenu();
	return s7_t(sc);
}

/* Scheme: (set-menu-text-color "#RRGGBB") - app menu text */
static s7_pointer scm_set_menu_text_color(s7_scheme *sc, s7_pointer args) {
	if (!s7_is_string(s7_car(args))) return s7_f(sc);
	cfg_menu_text_color = parse_color_rgb(s7_string(s7_car(args)));
	updateappmenu();
	return s7_t(sc);
}

/* Scheme: (toggle-appmenu) - toggle app menu visibility */
static s7_pointer scm_toggle_appmenu(s7_scheme *sc, s7_pointer args) {
	toggleappmenu(NULL);
	return s7_t(sc);
}

/* Scheme: (set-menu-button "text") - set the app menu button label */
static s7_pointer scm_set_menu_button(s7_scheme *sc, s7_pointer args) {
	if (!s7_is_string(s7_car(args))) return s7_f(sc);
	strncpy(cfg_menu_button, s7_string(s7_car(args)), sizeof(cfg_menu_button) - 1);
	cfg_menu_button[sizeof(cfg_menu_button) - 1] = '\0';
	updatebars();
	return s7_t(sc);
}

/* ========== END COLOR CONFIG API ========== */

/* Scheme: (set-tag-count n) - number of virtual desktops (1-9) */
static s7_pointer scm_set_tag_count(s7_scheme *sc, s7_pointer args) {
	int n;
	if (!s7_is_integer(s7_car(args))) return s7_f(sc);
	n = s7_integer(s7_car(args));
	if (n < 1) n = 1;
	if (n > 9) n = 9;
	cfg_tagcount = n;
	updatebars();
	return s7_t(sc);
}

/* Scheme: (set-show-time b) - show/hide time in status bar */
static s7_pointer scm_set_show_time(s7_scheme *sc, s7_pointer args) {
	cfg_show_time = s7_boolean(sc, s7_car(args)) ? 1 : 0;
	updatebars();
	return s7_t(sc);
}

/* Scheme: (set-show-date b) - show/hide date in status bar */
static s7_pointer scm_set_show_date(s7_scheme *sc, s7_pointer args) {
	cfg_show_date = s7_boolean(sc, s7_car(args)) ? 1 : 0;
	updatebars();
	return s7_t(sc);
}

/* Scheme: (set-status-text "text") - custom status text (replaces date/time if non-empty) */
static s7_pointer scm_set_status_text(s7_scheme *sc, s7_pointer args) {
	if (!s7_is_string(s7_car(args))) return s7_f(sc);
	strncpy(cfg_status_text, s7_string(s7_car(args)), sizeof(cfg_status_text) - 1);
	cfg_status_text[sizeof(cfg_status_text) - 1] = '\0';
	updatebars();
	return s7_t(sc);
}

/* Scheme: (on-startup cmd1 cmd2 ...) - register commands to run on startup */
static s7_pointer scm_on_startup(s7_scheme *sc, s7_pointer args) {
	s7_pointer arg;
	/* Clear existing startup commands */
	for (int i = 0; i < cfg_startup_cmd_count; i++) {
		free(cfg_startup_cmds[i]);
		cfg_startup_cmds[i] = NULL;
	}
	cfg_startup_cmd_count = 0;
	
	/* Collect all string arguments */
	for (arg = args; s7_is_pair(arg) && cfg_startup_cmd_count < MAX_STARTUP_CMDS; arg = s7_cdr(arg)) {
		if (s7_is_string(s7_car(arg))) {
			cfg_startup_cmds[cfg_startup_cmd_count] = strdup(s7_string(s7_car(arg)));
			cfg_startup_cmd_count++;
		}
	}
	tbwm_log(TBWM_LOG_INFO, "tbwm: on-startup: registered %d commands\n", cfg_startup_cmd_count);
	return s7_t(sc);
}

/* Run startup commands (called once after compositor is ready) */
static void run_startup_commands(void) {
	int i;
	if (cfg_startup_ran)
		return;
	cfg_startup_ran = 1;
	
	for (i = 0; i < cfg_startup_cmd_count; i++) {
		if (cfg_startup_cmds[i]) {
			tbwm_log(TBWM_LOG_INFO, "tbwm: on-startup: spawning %s\n", cfg_startup_cmds[i]);
			Arg a = { .v = (const char*[]){ "/bin/sh", "-c", cfg_startup_cmds[i], NULL } };
			spawn(&a);
		}
	}
}

/* ========== END NEW CONFIG API ========== */

/* Scheme: (set-font path size) */
static s7_pointer scm_set_font(s7_scheme *sc, s7_pointer args) {
	const char *path;
	int size;
	if (!s7_is_string(s7_car(args)) || !s7_is_integer(s7_cadr(args)))
		return s7_f(sc);
	path = s7_string(s7_car(args));
	size = s7_integer(s7_cadr(args));
	strncpy(cfg_font_path, path, sizeof(cfg_font_path) - 1);
	cfg_font_size = size;
	/* Reinitialize font */
	if (ft_face) FT_Done_Face(ft_face);
	if (FT_New_Face(ft_library, cfg_font_path, 0, &ft_face) == 0) {
		FT_Set_Pixel_Sizes(ft_face, 0, cfg_font_size);
		if (FT_Load_Char(ft_face, 'M', FT_LOAD_DEFAULT) == 0) {
			cell_width = ft_face->glyph->advance.x >> 6;
			cell_height = ft_face->size->metrics.height >> 6;
		}
		tbwm_log(TBWM_LOG_INFO, "tbwm: font changed to %s %d (%dx%d cells)\n", cfg_font_path, cfg_font_size, cell_width, cell_height);
		updateframes();
		updatebars();
	}
	return s7_t(sc);
}

/* Scheme: (set-repeat-rate rate delay) */
static s7_pointer scm_set_repeat_rate(s7_scheme *sc, s7_pointer args) {
	if (!s7_is_integer(s7_car(args)) || !s7_is_integer(s7_cadr(args)))
		return s7_f(sc);
	cfg_repeat_rate = s7_integer(s7_car(args));
	cfg_repeat_delay = s7_integer(s7_cadr(args));
	if (kb_group)
		wlr_keyboard_set_repeat_info(&kb_group->wlr_group->keyboard, cfg_repeat_rate, cfg_repeat_delay);
	return s7_t(sc);
}

/* Scheme: (set-tap-to-click b) */
static s7_pointer scm_set_tap_to_click(s7_scheme *sc, s7_pointer args) {
	cfg_tap_to_click = s7_boolean(sc, s7_car(args)) ? 1 : 0;
	return s7_t(sc);
}

/* Scheme: (set-natural-scrolling b) */
static s7_pointer scm_set_natural_scrolling(s7_scheme *sc, s7_pointer args) {
	cfg_natural_scrolling = s7_boolean(sc, s7_car(args)) ? 1 : 0;
	return s7_t(sc);
}

/* Scheme: (set-accel-speed speed) - speed is -1.0 to 1.0 */
static s7_pointer scm_set_accel_speed(s7_scheme *sc, s7_pointer args) {
	if (!s7_is_real(s7_car(args))) return s7_f(sc);
	cfg_accel_speed = s7_real(s7_car(args));
	return s7_t(sc);
}

/* Scheme: (add-rule app-id title tags floating monitor)
 * app-id and title can be #f for "any" */
static s7_pointer scm_add_rule(s7_scheme *sc, s7_pointer args) {
	const char *app_id, *title;
	int tags, floating, monitor;
	
	if (cfg_rule_count >= MAX_RULES) {
		tbwm_log(TBWM_LOG_WARN, "tbwm-scm: too many rules\n");
		return s7_f(sc);
	}
	
	app_id = s7_is_string(s7_car(args)) ? s7_string(s7_car(args)) : NULL;
	title = s7_is_string(s7_cadr(args)) ? s7_string(s7_cadr(args)) : NULL;
	tags = s7_is_integer(s7_caddr(args)) ? s7_integer(s7_caddr(args)) : 0;
	floating = s7_boolean(sc, s7_cadddr(args)) ? 1 : 0;
	monitor = s7_is_integer(s7_list_ref(sc, args, 4)) ? s7_integer(s7_list_ref(sc, args, 4)) : -1;
	
	if (app_id)
		strncpy(cfg_rules[cfg_rule_count].id, app_id, sizeof(cfg_rules[cfg_rule_count].id) - 1);
	else
		cfg_rules[cfg_rule_count].id[0] = '\0';
	
	if (title)
		strncpy(cfg_rules[cfg_rule_count].title, title, sizeof(cfg_rules[cfg_rule_count].title) - 1);
	else
		cfg_rules[cfg_rule_count].title[0] = '\0';
	
	cfg_rules[cfg_rule_count].tags = tags;
	cfg_rules[cfg_rule_count].isfloating = floating;
	cfg_rules[cfg_rule_count].monitor = monitor;
	cfg_rule_count++;
	
	return s7_t(sc);
}

/* Scheme: (clear-rules) */
static s7_pointer scm_clear_rules(s7_scheme *sc, s7_pointer args) {
	cfg_rule_count = 0;
	return s7_t(sc);
}

/* Scheme: (bind-mouse "M-button1" callback) */
static s7_pointer scm_bind_mouse(s7_scheme *sc, s7_pointer args) {
	const char *spec, *rest;
	uint32_t mods, button = 0;
	s7_pointer callback;
	
	if (!s7_is_string(s7_car(args))) return s7_f(sc);
	spec = s7_string(s7_car(args));
	callback = s7_cadr(args);
	if (!s7_is_procedure(callback)) return s7_f(sc);
	
	mods = parse_modifiers(spec, &rest);
	
	if (strcmp(rest, "button1") == 0 || strcmp(rest, "BTN_LEFT") == 0)
		button = BTN_LEFT;
	else if (strcmp(rest, "button2") == 0 || strcmp(rest, "BTN_MIDDLE") == 0)
		button = BTN_MIDDLE;
	else if (strcmp(rest, "button3") == 0 || strcmp(rest, "BTN_RIGHT") == 0)
		button = BTN_RIGHT;
	else {
		tbwm_log(TBWM_LOG_WARN, "tbwm-scm: unknown button: %s\n", rest);
		return s7_f(sc);
	}
	
	/* Replace existing mouse binding if duplicate (mod+button) */
	for (int i = 0; i < cfg_mouse_binding_count; i++) {
		if (CLEANMASK(cfg_mouse_bindings[i].mod) == CLEANMASK(mods) && cfg_mouse_bindings[i].button == button) {
			if (cfg_mouse_bindings[i].gc_loc >= 0)
				s7_gc_unprotect_at(sc, cfg_mouse_bindings[i].gc_loc);
			s7_int gc_loc = s7_gc_protect(sc, callback);
			cfg_mouse_bindings[i].callback = callback;
			cfg_mouse_bindings[i].gc_loc = gc_loc;
			file_debug_log("tbwm-scm: replaced mouse binding %s (idx=%d)\n", spec, i);
			return s7_t(sc);
		}
	}

	/* Ensure capacity and append new mouse binding */
	ensure_mouse_bindings_capacity(1);
	s7_int gc_loc = s7_gc_protect(sc, callback);
	cfg_mouse_bindings[cfg_mouse_binding_count].mod = mods;
	cfg_mouse_bindings[cfg_mouse_binding_count].button = button;
	cfg_mouse_bindings[cfg_mouse_binding_count].callback = callback;
	cfg_mouse_bindings[cfg_mouse_binding_count].gc_loc = gc_loc;
	cfg_mouse_binding_count++;
	file_debug_log("tbwm-scm: bound mouse %s (mod=0x%x, btn=%u)\n", spec, mods, button);
	return s7_t(sc);
}

/* Internal: unprotect and clear mouse bindings */
static void
clear_mouse_bindings_internal(void)
{
	int i;
	for (i = 0; i < cfg_mouse_binding_count; i++) {
		if (cfg_mouse_bindings[i].gc_loc >= 0)
			s7_gc_unprotect_at(sc, cfg_mouse_bindings[i].gc_loc);
		cfg_mouse_bindings[i].callback = s7_nil(sc);
		cfg_mouse_bindings[i].gc_loc = -1;
	}
	cfg_mouse_binding_count = 0;
}

/* Scheme: (clear-mouse-bindings) */
static s7_pointer scm_clear_mouse_bindings(s7_scheme *sc, s7_pointer args) {
	clear_mouse_bindings_internal();
	return s7_t(sc);
}

/* Scheme: (buffer-stats) - return buffer allocation stats for debugging */
static s7_pointer scm_buffer_stats(s7_scheme *sc, s7_pointer args) {
	char buf[512];
	int buf_leaked = titlebuf_alloc_count - titlebuf_free_count;
	int glyph_leaked = glyph_malloc_count - glyph_free_count;
	snprintf(buf, sizeof(buf), "buf: alloc=%d free=%d leaked=%d | glyph: malloc=%d free=%d leaked=%d bytes=%zu", 
	         titlebuf_alloc_count, titlebuf_free_count, buf_leaked,
	         glyph_malloc_count, glyph_free_count, glyph_leaked, glyph_total_bytes);
	return s7_make_string(sc, buf);
}

/* ==================== END SCHEME CONFIG SETTERS ====================  */

void
setup_scheme(void)
{
	/* Define Scheme functions */
	s7_define_function(sc, "spawn", scm_spawn, 1, 0, false, "(spawn cmd) launch a program");
	s7_define_function(sc, "quit", scm_quit, 0, 0, false, "(quit) exit the window manager");
	s7_define_function(sc, "focus-dir", scm_focus_dir, 1, 0, false, "(focus-dir dir) focus window in direction (0=left,1=right,2=up,3=down)");
	s7_define_function(sc, "swap-dir", scm_swap_dir, 1, 0, false, "(swap-dir dir) swap window in direction");
	s7_define_function(sc, "view-tag", scm_view_tag, 1, 0, false, "(view-tag n) switch to tag n (1-9)");
	s7_define_function(sc, "tag-window", scm_tag_window, 1, 0, false, "(tag-window n) move focused window to tag n");
	s7_define_function(sc, "toggle-floating", scm_toggle_floating, 0, 0, false, "(toggle-floating) toggle floating for focused window");
	s7_define_function(sc, "toggle-fullscreen", scm_toggle_fullscreen, 0, 0, false, "(toggle-fullscreen) toggle fullscreen for focused window");
	s7_define_function(sc, "kill-client", scm_kill_client, 0, 0, false, "(kill-client) close focused window");
	s7_define_function(sc, "refresh", scm_refresh, 0, 0, false, "(refresh) refresh layout");
	s7_define_function(sc, "toggle-launcher", scm_toggle_launcher, 0, 0, false, "(toggle-launcher) open/close launcher");
	s7_define_function(sc, "toggle-repl", scm_toggle_repl, 0, 0, false, "(toggle-repl) open/close Scheme REPL");
	s7_define_function(sc, "set-repl-log-level", scm_set_repl_log_level, 1, 0, false, "(set-repl-log-level n) set REPL log threshold (0=DEBUG,1=INFO,2=WARN,3=ERROR)");
	s7_define_function(sc, "move-window", scm_move_window, 0, 0, false, "(move-window) start moving focused window with mouse");
	s7_define_function(sc, "resize-window", scm_resize_window, 0, 0, false, "(resize-window) start resizing focused window with mouse");
	s7_define_function(sc, "focus-monitor", scm_focus_monitor, 1, 0, false, "(focus-monitor dir) focus monitor in direction");
	s7_define_function(sc, "tag-monitor", scm_tag_monitor, 1, 0, false, "(tag-monitor dir) send window to monitor");
	s7_define_function(sc, "bind-key", scm_bind_key, 2, 0, false, "(bind-key keyspec callback) bind key to function");
	s7_define_function(sc, "unbind-all", scm_unbind_all, 0, 0, false, "(unbind-all) remove all Scheme keybindings");

	/* Layout control */
	s7_define_function(sc, "set-layout", scm_set_layout, 1, 0, false, "(set-layout name) set layout: tile/dwindle/monocle/float");
	s7_define_function(sc, "cycle-layout", scm_cycle_layout, 0, 0, false, "(cycle-layout) cycle through layouts");
	s7_define_function(sc, "inc-mfact", scm_inc_mfact, 1, 0, false, "(inc-mfact delta) adjust master factor");
	s7_define_function(sc, "inc-nmaster", scm_inc_nmaster, 1, 0, false, "(inc-nmaster delta) adjust number of masters");
	s7_define_function(sc, "zoom", scm_zoom, 0, 0, false, "(zoom) swap focused with master");
	s7_define_function(sc, "focus-stack", scm_focus_stack, 1, 0, false, "(focus-stack delta) focus next/prev in stack");

	/* Tag operations */
	s7_define_function(sc, "toggle-tag", scm_toggle_tag, 1, 0, false, "(toggle-tag n) toggle tag visibility");
	s7_define_function(sc, "toggle-window-tag", scm_toggle_window_tag, 1, 0, false, "(toggle-window-tag n) toggle tag on window");
	s7_define_function(sc, "view-all", scm_view_all, 0, 0, false, "(view-all) view all tags");
	s7_define_function(sc, "tag-all", scm_tag_all, 0, 0, false, "(tag-all) set window to all tags");

	/* Meta */
	s7_define_function(sc, "eval-string", scm_eval_string, 1, 0, false, "(eval-string str) evaluate Scheme code");
	s7_define_function(sc, "reload-config", scm_reload_config, 0, 0, false, "(reload-config) reload config file");

	/* Queries */
	s7_define_function(sc, "focused-app-id", scm_focused_app_id, 0, 0, false, "(focused-app-id) get app_id of focused window");
	s7_define_function(sc, "focused-title", scm_focused_title, 0, 0, false, "(focused-title) get title of focused window");
	s7_define_function(sc, "current-tag", scm_current_tag, 0, 0, false, "(current-tag) get current tag number");
	s7_define_function(sc, "window-count", scm_window_count, 0, 0, false, "(window-count) get number of visible windows");
	s7_define_function(sc, "log", scm_log, 1, 0, false, "(log msg) print message to stderr");
	s7_define_function(sc, "help", scm_help, 0, 0, false, "(help) show available commands");
	s7_define_function(sc, "chvt", scm_chvt, 1, 0, false, "(chvt n) switch to virtual terminal n");
	s7_define_function(sc, "set-title-scroll-mode", scm_set_title_scroll_mode, 1, 0, false, "(set-title-scroll-mode mode) set title overflow mode: 0=truncate, 1=scroll");
	s7_define_function(sc, "set-title-scroll-speed", scm_set_title_scroll_speed, 1, 0, false, "(set-title-scroll-speed speed) set scroll speed in pixels per tick");

	/* Configuration setters - NEW CLEAN API */
	s7_define_function(sc, "set-bg-color", scm_set_bg_color, 1, 0, false, "(set-bg-color \"#RRGGBB\") set background/REPL color");
	s7_define_function(sc, "set-bg-text-color", scm_set_bg_text_color, 1, 0, false, "(set-bg-text-color \"#RRGGBB\") set background/REPL text color");
	s7_define_function(sc, "set-bar-color", scm_set_bar_color, 1, 0, false, "(set-bar-color \"#RRGGBB\") set status bar background color");
	s7_define_function(sc, "set-bar-text-color", scm_set_bar_text_color, 1, 0, false, "(set-bar-text-color \"#RRGGBB\") set status bar text color");
	s7_define_function(sc, "set-border-color", scm_set_border_color, 1, 0, false, "(set-border-color \"#RRGGBB\") set window highlight/border background color");
	s7_define_function(sc, "set-border-line-color", scm_set_border_line_color, 1, 0, false, "(set-border-line-color \"#RRGGBB\") set box-drawing border line color");
	s7_define_function(sc, "set-menu-color", scm_set_menu_color, 1, 0, false, "(set-menu-color \"#RRGGBB\") set app menu background color");
	s7_define_function(sc, "set-menu-text-color", scm_set_menu_text_color, 1, 0, false, "(set-menu-text-color \"#RRGGBB\") set app menu text color");
	s7_define_function(sc, "set-menu-button", scm_set_menu_button, 1, 0, false, "(set-menu-button \"text\") set app menu button label in bar");
	s7_define_function(sc, "toggle-appmenu", scm_toggle_appmenu, 0, 0, false, "(toggle-appmenu) toggle the app menu visibility");
	s7_define_function(sc, "set-tag-count", scm_set_tag_count, 1, 0, false, "(set-tag-count n) set number of virtual desktops (1-9)");
	s7_define_function(sc, "set-show-time", scm_set_show_time, 1, 0, false, "(set-show-time b) show/hide time in status bar");
	s7_define_function(sc, "set-show-date", scm_set_show_date, 1, 0, false, "(set-show-date b) show/hide date in status bar");
	s7_define_function(sc, "set-status-text", scm_set_status_text, 1, 0, false, "(set-status-text \"text\") custom status text (replaces date/time)");
	s7_define_function(sc, "set-sloppy-focus", scm_set_sloppy_focus, 1, 0, false, "(set-sloppy-focus b) enable/disable focus follows mouse");
	s7_define_function(sc, "on-startup", scm_on_startup, 0, 0, true, "(on-startup cmd1 cmd2 ...) register commands to run on startup");
	s7_define_function(sc, "buffer-stats", scm_buffer_stats, 0, 0, false, "(buffer-stats) show buffer alloc/free counts for leak detection");
	
	/* Font and input */
	s7_define_function(sc, "set-font", scm_set_font, 2, 0, false, "(set-font path size) set grid font");
	s7_define_function(sc, "set-repeat-rate", scm_set_repeat_rate, 2, 0, false, "(set-repeat-rate rate delay) set key repeat");
	s7_define_function(sc, "set-tap-to-click", scm_set_tap_to_click, 1, 0, false, "(set-tap-to-click b) enable/disable tap to click");
	s7_define_function(sc, "set-natural-scrolling", scm_set_natural_scrolling, 1, 0, false, "(set-natural-scrolling b) enable/disable natural scrolling");
	s7_define_function(sc, "set-accel-speed", scm_set_accel_speed, 1, 0, false, "(set-accel-speed n) set mouse acceleration (-1.0 to 1.0)");
	
	/* Window rules */
	s7_define_function(sc, "add-rule", scm_add_rule, 5, 0, false, "(add-rule app-id title tags floating monitor) add window rule");
	s7_define_function(sc, "clear-rules", scm_clear_rules, 0, 0, false, "(clear-rules) clear all window rules");
	
	/* Mouse bindings */
	s7_define_function(sc, "bind-mouse", scm_bind_mouse, 2, 0, false, "(bind-mouse \"M-button1\" callback) bind mouse button");
	s7_define_function(sc, "clear-mouse-bindings", scm_clear_mouse_bindings, 0, 0, false, "(clear-mouse-bindings) clear all mouse bindings");

	/* Define constants for directions */
	s7_define_variable(sc, "DIR-LEFT", s7_make_integer(sc, 0));
	s7_define_variable(sc, "DIR-RIGHT", s7_make_integer(sc, 1));
	s7_define_variable(sc, "DIR-UP", s7_make_integer(sc, 2));
	s7_define_variable(sc, "DIR-DOWN", s7_make_integer(sc, 3));

	/* Define constants for monitor directions */
	s7_define_variable(sc, "MON-LEFT", s7_make_integer(sc, WLR_DIRECTION_LEFT));
	s7_define_variable(sc, "MON-RIGHT", s7_make_integer(sc, WLR_DIRECTION_RIGHT));
}

static const char *default_config_parts[] = {
";;; TurboWM config.scm - Scheme configuration\n"
";;; All settings can be changed at runtime with (reload-config)\n"
"\n"
";;;; ==================== APPEARANCE ====================\n"
"\n"
";; Colors - Format: #RRGGBB\n"
";; Background/REPL color (black)\n"
"(set-bg-color \"#000000\")\n"
"\n"
";; Background/REPL text color (grey)\n"
"(set-bg-text-color \"#888888\")\n"
"\n"
";; Status bar background (the blue)\n"
"(set-bar-color \"#0000aa\")\n"
"\n"
";; Status bar text color (grey)\n"
"(set-bar-text-color \"#aaaaaa\")\n"
"\n"
";; Window highlight/border background (the blue)\n"
"(set-border-color \"#0000aa\")\n"
"\n"
";; Box-drawing border lines (the grey)\n"
"(set-border-line-color \"#aaaaaa\")\n"
"\n"
";; Number of virtual desktops/tags (1-9)\n"
"(set-tag-count 9)\n"
"\n"
";;;; ==================== STARTUP COMMANDS ====================\n"
"\n"
";; Commands to run when the compositor starts\n"
";; Uncomment and customize as needed:\n"
";; (on-startup \"waybar\" \"mako\" \"foot --server\")\n"
"\n"
";;;; ==================== STATUS BAR ====================\n"
"\n"
";; Show date and time in status bar\n"
"(set-show-date #t)\n"
"(set-show-time #t)\n"
"\n"
";; Custom status text (if set, replaces date/time)\n"
";; Use this from scripts to show anything: battery, wifi, etc.\n"
";; (set-status-text \"Battery: 85%\")\n"
"\n"
";; Title bar scrolling for long titles\n"
"(set-title-scroll-mode 1)   ; 1 = scroll, 0 = truncate with ...\n"
"(set-title-scroll-speed 30) ; pixels per second\n"
"\n"
";;;; ==================== BEHAVIOR ====================\n"
"\n"
";; Focus follows mouse (sloppy focus)\n"
"(set-sloppy-focus #t)\n"
"\n"
";; Keyboard repeat rate and delay (chars/sec, ms before repeat)\n"
"(set-repeat-rate 25 600)\n"
"\n"
";;;; ==================== INPUT DEVICES ====================\n"
"\n"
";; Trackpad settings\n"
"(set-tap-to-click #t)\n"
"(set-natural-scrolling #f)\n"
"(set-accel-speed 0.0)  ; -1.0 to 1.0\n"
"\n"
";;;; ==================== MOUSE BINDINGS ====================\n"
";; (bind-mouse \"MODIFIER-button\" callback)\n"
";; Buttons: button1 (left), button2 (middle), button3 (right)\n"
"\n"
";; Super+Left: move window\n"
"(bind-mouse \"M-button1\" (lambda () (move-window)))\n"
";; Super+Right: resize window\n"
"(bind-mouse \"M-button3\" (lambda () (resize-window)))\n"
"\n"
";;;; ==================== KEYBINDINGS ====================\n"
";; Modifiers: M = Super, S = Shift, C = Control, A = Alt\n"
"\n"
";; Terminal\n"
"(bind-key \"M-Return\" (lambda () (spawn \"foot\")))\n"
"\n"
";; Launcher\n"
"(bind-key \"M-d\" (lambda () (toggle-launcher)))\n"
"\n"
";; App menu\n"
"(bind-key \"M-x\" (lambda () (toggle-appmenu)))\n"
"\n"
";; Close window\n"
"(bind-key \"M-q\" (lambda () (kill-client)))\n"
"\n"
";; Quit TurboWM\n"
"(bind-key \"M-S-e\" (lambda () (quit)))\n"
"\n"
";; Focus direction (vim keys)\n"
"(bind-key \"M-h\" (lambda () (focus-dir DIR-LEFT)))\n"
"(bind-key \"M-j\" (lambda () (focus-dir DIR-DOWN)))\n"
"(bind-key \"M-k\" (lambda () (focus-dir DIR-UP)))\n"
"(bind-key \"M-l\" (lambda () (focus-dir DIR-RIGHT)))\n"
"\n"
";; Focus direction (arrow keys)\n"
"(bind-key \"M-Left\" (lambda () (focus-dir DIR-LEFT)))\n"
"(bind-key \"M-Down\" (lambda () (focus-dir DIR-DOWN)))\n"
"(bind-key \"M-Up\" (lambda () (focus-dir DIR-UP)))\n"
"(bind-key \"M-Right\" (lambda () (focus-dir DIR-RIGHT)))\n"
"\n"
";; Swap windows (vim keys)\n"
"(bind-key \"M-S-h\" (lambda () (swap-dir DIR-LEFT)))\n"
"(bind-key \"M-S-j\" (lambda () (swap-dir DIR-DOWN)))\n"
"(bind-key \"M-S-k\" (lambda () (swap-dir DIR-UP)))\n"
"(bind-key \"M-S-l\" (lambda () (swap-dir DIR-RIGHT)))\n"
"(bind-key \"M-S-Left\" (lambda () (swap-dir DIR-LEFT)))\n"
"(bind-key \"M-S-Down\" (lambda () (swap-dir DIR-DOWN)))\n"
"(bind-key \"M-S-Up\" (lambda () (swap-dir DIR-UP)))\n"
"(bind-key \"M-S-Right\" (lambda () (swap-dir DIR-RIGHT)))\n",

";; Fullscreen and floating\n"
"(bind-key \"M-f\" (lambda () (toggle-fullscreen)))\n"
"(bind-key \"M-S-space\" (lambda () (toggle-floating)))\n"
"\n"
";; Tags 1-9\n"
"(bind-key \"M-1\" (lambda () (view-tag 1)))\n"
"(bind-key \"M-2\" (lambda () (view-tag 2)))\n"
"(bind-key \"M-3\" (lambda () (view-tag 3)))\n"
"(bind-key \"M-4\" (lambda () (view-tag 4)))\n"
"(bind-key \"M-5\" (lambda () (view-tag 5)))\n"
"(bind-key \"M-6\" (lambda () (view-tag 6)))\n"
"(bind-key \"M-7\" (lambda () (view-tag 7)))\n"
"(bind-key \"M-8\" (lambda () (view-tag 8)))\n"
"(bind-key \"M-9\" (lambda () (view-tag 9)))\n"
"\n"
";; Move window to tag (Shift+number gives symbols on US keyboard)\n"
"(bind-key \"M-S-exclam\" (lambda () (tag-window 1)))\n"
"(bind-key \"M-S-at\" (lambda () (tag-window 2)))\n"
"(bind-key \"M-S-numbersign\" (lambda () (tag-window 3)))\n"
"(bind-key \"M-S-dollar\" (lambda () (tag-window 4)))\n"
"(bind-key \"M-S-percent\" (lambda () (tag-window 5)))\n"
"(bind-key \"M-S-asciicircum\" (lambda () (tag-window 6)))\n"
"(bind-key \"M-S-ampersand\" (lambda () (tag-window 7)))\n"
"(bind-key \"M-S-asterisk\" (lambda () (tag-window 8)))\n"
"(bind-key \"M-S-parenleft\" (lambda () (tag-window 9)))\n",

";; Refresh layout\n"
"(bind-key \"M-S-r\" (lambda () (refresh)))\n"
"\n"
";; Reload config (hot reload!)\n"
"(bind-key \"M-S-c\" (lambda () (reload-config) (log \"Config reloaded!\")))\n"
"\n"
";; Screenshots (requires grim, slurp, wl-copy)\n"
"(bind-key \"Print\" (lambda () (spawn \"sh -c 'grim - | wl-copy'\")))\n"
"(bind-key \"S-Print\" (lambda () (spawn \"sh -c 'grim -g \\\"$(slurp)\\\" - | wl-copy'\")))\n"
"\n"
";; Volume control (requires wpctl/wireplumber)\n"
"(bind-key \"XF86AudioRaiseVolume\" (lambda () (spawn \"wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%+\")))\n"
"(bind-key \"XF86AudioLowerVolume\" (lambda () (spawn \"wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-\")))\n"
"(bind-key \"XF86AudioMute\" (lambda () (spawn \"wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle\")))\n"
"(bind-key \"XF86AudioMicMute\" (lambda () (spawn \"wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle\")))\n"
"\n"
";; REPL - Scheme console on the desktop\n"
";; Super+Shift+; (Win+:) to open, Escape to close\n"
"(bind-key \"M-S-colon\" (lambda () (toggle-repl)))\n"
"\n"
";; TTY switching (Ctrl+Alt+F1-F12)\n"
"(bind-key \"C-A-F1\" (lambda () (chvt 1)))\n"
"(bind-key \"C-A-F2\" (lambda () (chvt 2)))\n"
"(bind-key \"C-A-F3\" (lambda () (chvt 3)))\n"
"(bind-key \"C-A-F4\" (lambda () (chvt 4)))\n"
"(bind-key \"C-A-F5\" (lambda () (chvt 5)))\n"
"(bind-key \"C-A-F6\" (lambda () (chvt 6)))\n"
"(bind-key \"C-A-F7\" (lambda () (chvt 7)))\n"
"(bind-key \"C-A-F8\" (lambda () (chvt 8)))\n"
"(bind-key \"C-A-F9\" (lambda () (chvt 9)))\n"
"(bind-key \"C-A-F10\" (lambda () (chvt 10)))\n"
"(bind-key \"C-A-F11\" (lambda () (chvt 11)))\n"
"(bind-key \"C-A-F12\" (lambda () (chvt 12)))\n"
"\n"
";;; === Complete Scheme binding examples ===\n"
";; Control and window management\n"
"(bind-key \"M-Return\" (lambda () (spawn \"foot\")))\n"
"(bind-key \"M-d\" (lambda () (toggle-launcher)))\n"
"(bind-key \"M-x\" (lambda () (toggle-appmenu)))\n"
"(bind-key \"M-q\" (lambda () (kill-client)))\n"
"(bind-key \"M-S-e\" (lambda () (quit)))\n"
"\n"
";; Focus (vim & arrows)\n"
"(bind-key \"M-h\" (lambda () (focus-dir DIR-LEFT)))\n"
"(bind-key \"M-j\" (lambda () (focus-dir DIR-DOWN)))\n"
"(bind-key \"M-k\" (lambda () (focus-dir DIR-UP)))\n"
"(bind-key \"M-l\" (lambda () (focus-dir DIR-RIGHT)))\n"
"(bind-key \"M-Left\" (lambda () (focus-dir DIR-LEFT)))\n"
"(bind-key \"M-Down\" (lambda () (focus-dir DIR-DOWN)))\n"
"(bind-key \"M-Up\" (lambda () (focus-dir DIR-UP)))\n"
"(bind-key \"M-Right\" (lambda () (focus-dir DIR-RIGHT)))\n"
"\n"
";; Swap windows (vim & arrows)\n"
"(bind-key \"M-S-h\" (lambda () (swap-dir DIR-LEFT)))\n"
"(bind-key \"M-S-j\" (lambda () (swap-dir DIR-DOWN)))\n"
"(bind-key \"M-S-k\" (lambda () (swap-dir DIR-UP)))\n"
"(bind-key \"M-S-l\" (lambda () (swap-dir DIR-RIGHT)))\n"
"(bind-key \"M-S-Left\" (lambda () (swap-dir DIR-LEFT)))\n"
"(bind-key \"M-S-Down\" (lambda () (swap-dir DIR-DOWN)))\n"
"(bind-key \"M-S-Up\" (lambda () (swap-dir DIR-UP)))\n"
"(bind-key \"M-S-Right\" (lambda () (swap-dir DIR-RIGHT)))\n"
"\n"
";; Move/Resize with mouse or keyboard examples\n"
"(bind-mouse \"M-button1\" (lambda () (move-window)))\n"
"(bind-mouse \"M-button3\" (lambda () (resize-window)))\n"
"(bind-key \"M-S-m\" (lambda () (move-window))) ; example keybinding\n"
"(bind-key \"M-S-r\" (lambda () (resize-window))) ; example keybinding\n"
"\n"
";; Layouts & master area\n"
"(bind-key \"M-f\" (lambda () (set-layout \"float\")))\n"
"(bind-key \"M-S-\" (lambda () (cycle-layout))) ; use a key you like\n"
"(bind-key \"M-h\" (lambda () (inc-mfact -0.05))) ; shrink master (example)\n"
"(bind-key \"M-l\" (lambda () (inc-mfact 0.05))) ; grow master (example)\n"
"(bind-key \"M-0\" (lambda () (view-all))) ; view all\n"
"\n"
";; Tagging\n"
"(bind-key \"M-1\" (lambda () (view-tag 1)))\n"
"(bind-key \"M-S-exclam\" (lambda () (tag-window 1))) ; move to tag 1\n"
"\n"
";; REPL & reload\n"
"(bind-key \"M-S-colon\" (lambda () (toggle-repl)))\n"
"(bind-key \"M-S-c\" (lambda () (reload-config)))\n"
"(bind-key \"M-S-r\" (lambda () (refresh)))\n"
"(bind-key \"M-;\" (lambda () (set-repl-log-level 1))) ; example to set repl log level to INFO\n"
"\n"
";; Misc: toggle fullscreen/floating, kill, help\n"
"(bind-key \"M-f\" (lambda () (toggle-fullscreen)))\n"
"(bind-key \"M-S-space\" (lambda () (toggle-floating)))\n"
"(bind-key \"M-k\" (lambda () (kill-client)))\n"
"(bind-key \"M-h\" (lambda () (help))) ; show help in REPL\n"
"\n"
";; Advanced setters (examples - not necessarily key bound):\n"
";; (set-border-width 2)\n"
";; (set-border-color \"#FF0000\")\n"
";; (set-frame-bg-color \"#FF0000FF\")\n"
";; (set-font \"/path/to/font.ttf\" 16)\n"
"\n"
";;;; ==================== EXTERNAL CONFIG SYNC ====================\n"
";; Sync colors to external apps. Uncomment and modify as needed.\n"
";; This runs every time config is loaded/reloaded.\n"
"\n"
";; --- Foot terminal ---\n"
";; (let* ((home (getenv \"HOME\"))\n"
";;        (conf (string-append home \"/.config/foot/foot.ini\")))\n"
";;   (system (string-append \"mkdir -p \" home \"/.config/foot\"))\n"
";;   (if (file-exists? conf)\n"
";;       (system (string-append \"cp \" conf \" \" conf \".bak\")))\n"
";;   (call-with-output-file conf\n"
";;     (lambda (p)\n"
";;       (display \"[main]\\nfont=monospace:size=10\\n\\n[colors]\\n\" p)\n"
";;       (display \"background=000000\\nforeground=aaaaaa\\n\" p))))\n"
"\n"
"(log \"TurboWM config loaded!\")\n",
NULL
};

void
load_config(void)
{
	char path[1024], dir[512];
	const char *home = getenv("HOME");
	FILE *f;

	if (!home || !sc)
		return;

	snprintf(dir, sizeof(dir), "%s/.config/tbwm", home);
	snprintf(path, sizeof(path), "%s/config.scm", dir);
	
	f = fopen(path, "r");
	if (!f) {
		/* Create default config */
		tbwm_log(TBWM_LOG_INFO, "tbwm: creating default config at %s\n", path);
		
		/* Create directory */
		mkdir(dir, 0755);
		
		f = fopen(path, "w");
		if (f) {
			/* Write default config as multiple parts to avoid very-large single string literals */
			for (int i = 0; default_config_parts[i]; ++i)
				fputs(default_config_parts[i], f);
			fclose(f);
		} else {
			tbwm_log(TBWM_LOG_WARN, "tbwm: warning: could not create config file\n");
			/* Evaluate default config parts directly */
			for (int i = 0; default_config_parts[i]; ++i)
				s7_eval_c_string(sc, default_config_parts[i]);
			return;
		}
		f = fopen(path, "r");
	} 
	
	if (f) {
		fclose(f);
		tbwm_log(TBWM_LOG_INFO, "tbwm: loading config from %s\n", path);
		/* Clear existing Scheme and mouse bindings first to avoid duplicates on reload */
		tbwm_log(TBWM_LOG_INFO, "tbwm: clearing %d scheme bindings and %d mouse bindings before loading config\n", scheme_binding_count, cfg_mouse_binding_count);
		unbind_all_scheme_bindings();
		clear_mouse_bindings_internal();
		s7_load(sc, path);
		/* Ensure arrow swap bindings exist (guard against config truncation/parsing issues) */
		file_debug_log("tbwm-scm: ensuring M-S-Left/Right/Up/Down are bound\n");
		s7_eval_c_string(sc, "(bind-key \"M-S-Left\" (lambda () (swap-dir DIR-LEFT)))");
		s7_eval_c_string(sc, "(bind-key \"M-S-Right\" (lambda () (swap-dir DIR-RIGHT)))");
		s7_eval_c_string(sc, "(bind-key \"M-S-Up\" (lambda () (swap-dir DIR-UP)))");
		s7_eval_c_string(sc, "(bind-key \"M-S-Down\" (lambda () (swap-dir DIR-DOWN)))");
	} 
}

/* ==================== END SCHEME BINDINGS ==================== */

static const char *foot_config =
"# tbwm foot configuration\n"
"\n"
"[main]\n"
"font=PxPlus IBM VGA 8x16:size=12\n"
"\n"
"[colors]\n"
"# Classic DOS/VGA color scheme\n"
"background=000000\n"
"foreground=aaaaaa\n"
"\n"
"## Normal/regular colors (0-7)\n"
"regular0=000000  # black\n"
"regular1=aa0000  # red\n"
"regular2=00aa00  # green\n"
"regular3=aa5500  # yellow/brown\n"
"regular4=0000aa  # blue\n"
"regular5=aa00aa  # magenta\n"
"regular6=00aaaa  # cyan\n"
"regular7=aaaaaa  # white\n"
"\n"
"## Bright colors (8-15)\n"
"bright0=555555  # bright black\n"
"bright1=ff5555  # bright red\n"
"bright2=55ff55  # bright green\n"
"bright3=ffff55  # bright yellow\n"
"bright4=5555ff  # bright blue\n"
"bright5=ff55ff  # bright magenta\n"
"bright6=55ffff  # bright cyan\n"
"bright7=ffffff  # bright white\n"
"\n"
"[tweak]\n"
"font-monospace-warn=no\n";

static void
setup_foot_config(void)
{
	char path[1024], dir[512], backup[1024];
	char fontdir[512], fontsrc[512], fontdst[1024];
	const char *home = getenv("HOME");
	FILE *f, *src, *dst;
	struct stat st;
	char buf[4096];
	size_t n;

	if (!home)
		return;

	/* Install font if needed */
	snprintf(fontdir, sizeof(fontdir), "%s/.local/share/fonts", home);
	snprintf(fontsrc, sizeof(fontsrc), "%s/PxPlus_IBM_VGA_8x16.ttf", home);
	snprintf(fontdst, sizeof(fontdst), "%s/PxPlus_IBM_VGA_8x16.ttf", fontdir);

	if (stat(fontdst, &st) != 0 && stat(fontsrc, &st) == 0) {
		/* Create font directory */
		snprintf(path, sizeof(path), "%s/.local", home);
		mkdir(path, 0755);
		snprintf(path, sizeof(path), "%s/.local/share", home);
		mkdir(path, 0755);
		mkdir(fontdir, 0755);

		/* Copy font file */
		src = fopen(fontsrc, "rb");
		if (src) {
			dst = fopen(fontdst, "wb");
			if (dst) {
				while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
					fwrite(buf, 1, n, dst);
				fclose(dst);
				tbwm_log(TBWM_LOG_INFO, "tbwm: installed font to %s\n", fontdst);

				/* Update font cache */
				if (fork() == 0) {
					execlp("fc-cache", "fc-cache", "-f", fontdir, NULL);
					_exit(1);
				}
			}
			fclose(src);
		}
	}

	/* Setup foot config */
	snprintf(dir, sizeof(dir), "%s/.config/foot", home);
	snprintf(path, sizeof(path), "%s/foot.ini", dir);
	snprintf(backup, sizeof(backup), "%s/foot.ini.tbwm-backup", dir);

	/* Create directory if needed */
	if (stat(dir, &st) != 0)
		mkdir(dir, 0755);

	/* Back up existing config if it exists and no backup exists yet */
	if (stat(path, &st) == 0 && stat(backup, &st) != 0) {
		tbwm_log(TBWM_LOG_INFO, "tbwm: backing up existing foot.ini to %s\n", backup);
		rename(path, backup);
	}

	/* Write our config */
	f = fopen(path, "w");
	if (f) {
		fputs(foot_config, f);
		fclose(f);
		tbwm_log(TBWM_LOG_INFO, "tbwm: installed foot config at %s\n", path);
	} else {
		tbwm_log(TBWM_LOG_WARN, "tbwm: warning: could not write foot config\n");
	} 
}

static int
app_compare(const void *a, const void *b)
{
	return strcmp(*(const char **)a, *(const char **)b);
}

void
buildappcache(void)
{
	char *path, *path_copy, *dir;
	DIR *d;
	struct dirent *ent;
	int capacity = 256;
	int i, j, dup;

	if (app_cache) {
		for (i = 0; i < app_cache_count; i++)
			free(app_cache[i]);
		free(app_cache);
	}

	app_cache = ecalloc(capacity, sizeof(char *));
	app_cache_count = 0;

	path = getenv("PATH");
	if (!path)
		return;

	path_copy = strdup(path);
	dir = strtok(path_copy, ":");
	while (dir) {
		d = opendir(dir);
		if (d) {
			while ((ent = readdir(d))) {
				if (ent->d_name[0] == '.')
					continue;
				/* Check for duplicates */
				dup = 0;
				for (j = 0; j < app_cache_count; j++) {
					if (strcmp(app_cache[j], ent->d_name) == 0) {
						dup = 1;
						break;
					}
				}
				if (dup)
					continue;
				if (app_cache_count >= capacity) {
					size_t new_capacity = capacity * 2;
					char **tmp = realloc(app_cache, new_capacity * sizeof(char *));
					if (!tmp) {
						 tbwm_log(TBWM_LOG_WARN, "tbwm: warning: cannot grow app cache to %zu entries: %s\n", new_capacity, strerror(errno));
						/* stop adding further entries to avoid inconsistent state */
						break;
					}
					app_cache = tmp;
					capacity = new_capacity;
				}
				app_cache[app_cache_count++] = strdup(ent->d_name);
			}
			closedir(d);
		}
		dir = strtok(NULL, ":");
	}
	free(path_copy);

	qsort(app_cache, app_cache_count, sizeof(char *), app_compare);
	tbwm_log(TBWM_LOG_INFO, "Built app cache: %d entries", app_cache_count);
}

int
bartimer(void *data)
{
	/* Used for clock updates */
	updatebars();
	wl_event_source_timer_update(bar_timer, 1000);
	return 0;
}

int
scrolltimer(void *data)
{
	Client *c;
	int scroll_count = 0;
	
	/* Handle smooth scrolling for window titlebars only */
	if (!title_scroll_mode || !any_title_needs_scroll) {
		/* No scrolling needed, check again later */
		wl_event_source_timer_update(scroll_timer, 100);
		return 0;
	}
	
	/* Advance scroll offset (~1 pixel per 33ms at speed 30) */
	title_scroll_offset += 1;
	
	/* Only update windows that actually need scrolling */
	wl_list_for_each(c, &clients, link) {
		if (c->needs_title_scroll) {
			updateframe(c);
			scroll_count++;
		}
	}
	
	/* If no windows needed scrolling, reset the flag */
	if (scroll_count == 0)
		any_title_needs_scroll = 0;
	
	/* Continue at 30fps */
	wl_event_source_timer_update(scroll_timer, 33);
	return 0;
}

void
updatebars(void)
{
	Monitor *m;
	/* Don't update if not fully initialized */
	if (!layers[LyrOverlay])
		return;
	/* Reset scroll flag - will be set by updatebar/updateframe if needed */
	any_title_needs_scroll = 0;
	wl_list_for_each(m, &mons, link)
		updatebar(m);
}

void
updateframes(void)
{
	Client *c;
	/* Update all client frames for scrolling titles */
	if (!title_scroll_mode)
		return;
	wl_list_for_each(c, &clients, link)
		updateframe(c);
}

void
togglelauncher(const Arg *arg)
{
	launcher_active = !launcher_active;
	launcher_input[0] = '\0';
	launcher_input_len = 0;
	updatebars();
}

void
togglerepl(const Arg *arg)
{
	repl_input_active = !repl_input_active;
	repl_input[0] = '\0';
	repl_input_len = 0;
	updatebars();
	updaterepl();
}

/* Map freedesktop category to simplified category */
static const char *
map_category(const char *cats)
{
	if (!cats) return "Other";
	if (strstr(cats, "AudioVideo") || strstr(cats, "Audio") || strstr(cats, "Video"))
		return "Multimedia";
	if (strstr(cats, "Development") || strstr(cats, "IDE"))
		return "Development";
	if (strstr(cats, "Game"))
		return "Games";
	if (strstr(cats, "Graphics"))
		return "Graphics";
	if (strstr(cats, "Network") || strstr(cats, "WebBrowser") || strstr(cats, "Email"))
		return "Internet";
	if (strstr(cats, "Office") || strstr(cats, "WordProcessor") || strstr(cats, "Spreadsheet"))
		return "Office";
	if (strstr(cats, "Settings") || strstr(cats, "System") || strstr(cats, "Monitor"))
		return "System";
	if (strstr(cats, "Utility") || strstr(cats, "Accessibility"))
		return "Accessories";
	if (strstr(cats, "Education") || strstr(cats, "Science"))
		return "Education";
	return "Other";
}

/* Find or create category, returns index */
static int
find_or_create_category(const char *name)
{
	int i;
	for (i = 0; i < category_count; i++) {
		if (strcmp(categories[i].name, name) == 0)
			return i;
	}
	if (category_count < MAX_CATEGORIES) {
		strncpy(categories[category_count].name, name, CAT_NAME_LEN - 1);
		categories[category_count].name[CAT_NAME_LEN - 1] = '\0';
		categories[category_count].app_count = 0;
		return category_count++;
	}
	return -1;
}

/* Parse a single .desktop file */
static void
parse_desktop_file(const char *path)
{
	FILE *f;
	char line[512];
	char name[APP_NAME_LEN] = "";
	char exec[APP_EXEC_LEN] = "";
	char cats[256] = "";
	int nodisplay = 0;
	int in_desktop_entry = 0;
	
	f = fopen(path, "r");
	if (!f) return;
	
	while (fgets(line, sizeof(line), f)) {
		/* Remove newline */
		line[strcspn(line, "\n")] = 0;
		
		if (strcmp(line, "[Desktop Entry]") == 0) {
			in_desktop_entry = 1;
			continue;
		}
		if (line[0] == '[') {
			in_desktop_entry = 0;
			continue;
		}
		if (!in_desktop_entry) continue;
		
		if (strncmp(line, "Name=", 5) == 0 && name[0] == '\0') {
			strncpy(name, line + 5, APP_NAME_LEN - 1);
			name[APP_NAME_LEN - 1] = '\0';
		} else if (strncmp(line, "Exec=", 5) == 0) {
			/* Copy exec, removing %f %F %u %U etc */
			char *src = line + 5;
			char *dst = exec;
			char *end = exec + APP_EXEC_LEN - 1;
			while (*src && dst < end) {
				if (*src == '%' && src[1]) {
					src += 2; /* skip %X */
				} else {
					*dst++ = *src++;
				}
			}
			*dst = '\0';
			/* Trim trailing spaces */
			while (dst > exec && dst[-1] == ' ') *--dst = '\0';
		} else if (strncmp(line, "Categories=", 11) == 0) {
			strncpy(cats, line + 11, sizeof(cats) - 1);
			cats[sizeof(cats) - 1] = '\0';
		} else if (strncmp(line, "NoDisplay=true", 14) == 0) {
			nodisplay = 1;
		} else if (strncmp(line, "Hidden=true", 11) == 0) {
			nodisplay = 1;
		}
	}
	fclose(f);
	
	/* Add to list if valid */
	if (name[0] && exec[0] && !nodisplay && app_entry_count < MAX_APPS) {
		const char *cat = map_category(cats);
		int cat_idx = find_or_create_category(cat);
		
		strncpy(app_entries[app_entry_count].name, name, APP_NAME_LEN - 1);
		app_entries[app_entry_count].name[APP_NAME_LEN - 1] = '\0';
		strncpy(app_entries[app_entry_count].exec, exec, APP_EXEC_LEN - 1);
		app_entries[app_entry_count].exec[APP_EXEC_LEN - 1] = '\0';
		strncpy(app_entries[app_entry_count].category, cat, CAT_NAME_LEN - 1);
		app_entries[app_entry_count].category[CAT_NAME_LEN - 1] = '\0';
		
		if (cat_idx >= 0)
			categories[cat_idx].app_count++;
		
		app_entry_count++;
	}
}

/* Scan directory for .desktop files */
static void
scan_desktop_dir(const char *dir)
{
	DIR *d;
	struct dirent *entry;
	char path[512];
	
	d = opendir(dir);
	if (!d) return;
	
	while ((entry = readdir(d)) != NULL) {
		int len = strlen(entry->d_name);
		if (len > 8 && strcmp(entry->d_name + len - 8, ".desktop") == 0) {
			snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
			parse_desktop_file(path);
		}
	}
	closedir(d);
}

/* Compare apps by name for sorting */
static int
appentry_compare(const void *a, const void *b)
{
	return strcasecmp(((AppEntry*)a)->name, ((AppEntry*)b)->name);
}

/* Compare categories by name for sorting */
static int
cat_compare(const void *a, const void *b)
{
	return strcasecmp(((CategoryEntry*)a)->name, ((CategoryEntry*)b)->name);
}

/* Load all applications from .desktop files */
static void
load_applications(void)
{
	char *home;
	char local_apps[256];
	
	if (apps_loaded) return;
	
	app_entry_count = 0;
	category_count = 0;
	
	/* Scan system applications */
	scan_desktop_dir("/usr/share/applications");
	scan_desktop_dir("/usr/local/share/applications");
	
	/* Scan user applications */
	home = getenv("HOME");
	if (home) {
		snprintf(local_apps, sizeof(local_apps), "%s/.local/share/applications", home);
		scan_desktop_dir(local_apps);
	}
	
	/* Sort apps and categories */
	qsort(app_entries, app_entry_count, sizeof(AppEntry), appentry_compare);
	qsort(categories, category_count, sizeof(CategoryEntry), cat_compare);
	
	apps_loaded = 1;
	tbwm_log(TBWM_LOG_INFO, "Loaded %d applications in %d categories", app_entry_count, category_count);
}

void
toggleappmenu(const Arg *arg)
{
	appmenu_active = !appmenu_active;
	if (appmenu_active) {
		load_applications();
		menu_current_category = -1;
		menu_scroll_offset = 0;
		menu_selected_row = 0;
	}
	updateappmenu();
	updatebars();
}

void
updateappmenu(void)
{
	struct TitleBuffer *tb;
	uint32_t *pixels;
	int menu_cells_w = 25;
	int menu_cells_h = 25;
	int menu_width = menu_cells_w * cell_width;
	int menu_height = menu_cells_h * cell_height;
	int i, x, y, row, col;
	uint32_t frame_bg = RGB_TO_ARGB(cfg_border_color);
	uint32_t line_color = RGB_TO_ARGB(cfg_border_line_color);
	uint32_t content_bg = RGB_TO_ARGB(cfg_menu_color);
	uint32_t text_color = RGB_TO_ARGB(cfg_menu_text_color);
	
	/* If menu is not active, hide the buffer and return */
	if (!appmenu_active) {
		if (appmenu_buffer) {
			wlr_scene_node_set_enabled(&appmenu_buffer->node, 0);
		}
		return;
	}
	
	/* Reuse cached buffer or allocate new one */
	if (!appmenu_tb) {
		appmenu_tb = ecalloc(1, sizeof(*appmenu_tb));
		appmenu_tb->stride = menu_width * 4;
		appmenu_tb->data = ecalloc(1, appmenu_tb->stride * menu_height);
		wlr_buffer_init(&appmenu_tb->base, &titlebuf_impl, menu_width, menu_height);
		titlebuf_alloc_count++;
	}
	tb = appmenu_tb;
	pixels = tb->data;
	
	/* Fill entire background with content color first */
	for (i = 0; i < menu_width * menu_height; i++) {
		pixels[i] = content_bg;
	}
	
	/* Draw frame background for border cells (top row, bottom row, left col, right col) */
	/* Top row */
	for (y = 0; y < cell_height; y++) {
		for (x = 0; x < menu_width; x++) {
			pixels[y * menu_width + x] = frame_bg;
		}
	}
	/* Bottom row */
	for (y = (menu_cells_h - 1) * cell_height; y < menu_height; y++) {
		for (x = 0; x < menu_width; x++) {
			pixels[y * menu_width + x] = frame_bg;
		}
	}
	/* Left column */
	for (y = 0; y < menu_height; y++) {
		for (x = 0; x < cell_width; x++) {
			pixels[y * menu_width + x] = frame_bg;
		}
	}
	/* Right column */
	for (y = 0; y < menu_height; y++) {
		for (x = (menu_cells_w - 1) * cell_width; x < menu_width; x++) {
			pixels[y * menu_width + x] = frame_bg;
		}
	}
	
	/* Draw box-drawing characters for the frame */
	/* Top-left corner ╔ */
	render_char_to_buffer(pixels, menu_width, menu_height, 0, 0, 0x2554, line_color);
	/* Top-right corner ╗ */
	render_char_to_buffer(pixels, menu_width, menu_height, (menu_cells_w - 1) * cell_width, 0, 0x2557, line_color);
	/* Bottom-left corner ╚ */
	render_char_to_buffer(pixels, menu_width, menu_height, 0, (menu_cells_h - 1) * cell_height, 0x255A, line_color);
	/* Bottom-right corner ╝ */
	render_char_to_buffer(pixels, menu_width, menu_height, (menu_cells_w - 1) * cell_width, (menu_cells_h - 1) * cell_height, 0x255D, line_color);
	
	/* Top edge ═ with title "Menu" */
	{
		const char *title = "Menu";
		int title_len = strlen(title);
		int title_start = 2; /* start after ╔═ */
		for (col = 1; col < menu_cells_w - 1; col++) {
			if (col >= title_start && col < title_start + title_len) {
				render_char_to_buffer(pixels, menu_width, menu_height, col * cell_width, 0, title[col - title_start], line_color);
			} else {
				render_char_to_buffer(pixels, menu_width, menu_height, col * cell_width, 0, 0x2550, line_color);
			}
		}
	}
	/* Bottom edge ═ */
	for (col = 1; col < menu_cells_w - 1; col++) {
		render_char_to_buffer(pixels, menu_width, menu_height, col * cell_width, (menu_cells_h - 1) * cell_height, 0x2550, line_color);
	}
	/* Left edge ║ */
	for (row = 1; row < menu_cells_h - 1; row++) {
		render_char_to_buffer(pixels, menu_width, menu_height, 0, row * cell_height, 0x2551, line_color);
	}
	/* Right edge ║ */
	for (row = 1; row < menu_cells_h - 1; row++) {
		render_char_to_buffer(pixels, menu_width, menu_height, (menu_cells_w - 1) * cell_width, row * cell_height, 0x2551, line_color);
	}
	
	/* Draw content: categories or apps */
	{
		int content_rows = menu_cells_h - 2; /* rows available for content */
		int max_text_len = menu_cells_w - 2; /* space for text (minus borders) */
		uint32_t highlight_bg = RGB_TO_ARGB(cfg_border_color); /* blue highlight */
		uint32_t highlight_fg = RGB_TO_ARGB(cfg_border_line_color); /* grey text on blue */
		
		if (menu_current_category < 0) {
			/* Show categories */
			for (row = 0; row < content_rows && row + menu_scroll_offset < category_count; row++) {
				int item_idx = row + menu_scroll_offset;
				int text_y = (row + 1) * cell_height;
				int text_x = cell_width; /* 1 cell from left */
				const char *cat_name = categories[item_idx].name;
				int ci;
				int is_selected = (row == menu_selected_row);
				uint32_t row_fg = is_selected ? highlight_fg : text_color;
				
				/* Highlight background if selected */
				if (is_selected) {
					int px, py;
					for (py = text_y; py < text_y + cell_height; py++) {
						for (px = cell_width; px < menu_width - cell_width; px++) {
							pixels[py * menu_width + px] = highlight_bg;
						}
					}
				}
				
				/* Draw category name */
				for (ci = 0; cat_name[ci] && ci < max_text_len; ci++) {
					render_char_to_buffer(pixels, menu_width, menu_height,
						text_x + ci * cell_width, text_y,
						cat_name[ci], row_fg);
				}
			}
		} else {
			/* Show apps in selected category */
			const char *selected_cat = categories[menu_current_category].name;
			int app_idx = 0;
			int displayed = 0;
			int is_selected;
			uint32_t row_fg;
			
			/* First row: "< Back" */
			is_selected = (menu_selected_row == 0);
			row_fg = is_selected ? highlight_fg : text_color;
			{
				int text_y = cell_height;
				const char *back = "< Back";
				int bi;
				
				if (is_selected) {
					int px, py;
					for (py = text_y; py < text_y + cell_height; py++) {
						for (px = cell_width; px < menu_width - cell_width; px++) {
							pixels[py * menu_width + px] = highlight_bg;
						}
					}
				}
				
				for (bi = 0; back[bi] && bi < max_text_len; bi++) {
					render_char_to_buffer(pixels, menu_width, menu_height,
						cell_width + bi * cell_width, text_y,
						back[bi], row_fg);
				}
			}
			
			/* Show apps in this category */
			for (i = 0; i < app_entry_count && displayed < content_rows - 1; i++) {
				if (strcmp(app_entries[i].category, selected_cat) == 0) {
					if (app_idx >= menu_scroll_offset) {
						int text_y = (displayed + 2) * cell_height;
						int text_x = cell_width;
						const char *app_name = app_entries[i].name;
						int ai;
						
						is_selected = (displayed + 1 == menu_selected_row);
						row_fg = is_selected ? highlight_fg : text_color;
						
						if (is_selected) {
							int px, py;
							for (py = text_y; py < text_y + cell_height; py++) {
								for (px = cell_width; px < menu_width - cell_width; px++) {
									pixels[py * menu_width + px] = highlight_bg;
								}
							}
						}
						
						/* Draw app name */
						for (ai = 0; app_name[ai] && ai < max_text_len; ai++) {
							render_char_to_buffer(pixels, menu_width, menu_height,
								text_x + ai * cell_width, text_y,
								app_name[ai], row_fg);
						}
						displayed++;
					}
					app_idx++;
				}
			}
		}
	}
	
	/* Create or update the appmenu buffer */
	if (!appmenu_buffer)
		appmenu_buffer = wlr_scene_buffer_create(layers[LyrTop], NULL);
	wlr_scene_node_set_enabled(&appmenu_buffer->node, 1);
	/* Position at top-left of focused monitor, below the bar */
	if (selmon) {
		wlr_scene_node_set_position(&appmenu_buffer->node, selmon->m.x, selmon->m.y + cell_height);
	} else {
		wlr_scene_node_set_position(&appmenu_buffer->node, sgeom.x, sgeom.y + cell_height);
	}
	wlr_scene_buffer_set_buffer(appmenu_buffer, &tb->base);
	/* Don't drop - we're caching the buffer for reuse */
}

/* Key helper: reload configuration (wrapper so we can bind it in C defaults) */
void
reload_config_key(const Arg *arg)
{
	load_config();
	tbwm_log(TBWM_LOG_INFO, "tbwm: config reloaded via keybinding\n");
}

void
repl_add_line(const char *line)
{
	if (repl_history_count < REPL_HISTORY_LINES) {
		strncpy(repl_history[repl_history_count], line, REPL_LINE_LEN - 1);
		repl_history[repl_history_count][REPL_LINE_LEN - 1] = '\0';
		repl_history_count++;
	} else {
		/* Scroll buffer up */
		int i;
		for (i = 0; i < REPL_HISTORY_LINES - 1; i++) {
			strcpy(repl_history[i], repl_history[i + 1]);
		}
		strncpy(repl_history[REPL_HISTORY_LINES - 1], line, REPL_LINE_LEN - 1);
		repl_history[REPL_HISTORY_LINES - 1][REPL_LINE_LEN - 1] = '\0';
	}
	repl_scroll_offset = 0;
	updaterepl();
}

void
tbwm_log(int level, const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	/* Always write to debug file */
	file_debug_log("%s", buf);

	/* Also emit to stderr so external libraries still see the messages */
	fprintf(stderr, "%s\n", buf);

	/* Send warnings/errors to REPL (trim to REPL_LINE_LEN-1)
	 * Note: REPL only receives lines with level >= cfg_repl_log_level */
	if (level >= cfg_repl_log_level) {
		char line[REPL_LINE_LEN];
		strncpy(line, buf, REPL_LINE_LEN - 1);
		line[REPL_LINE_LEN - 1] = '\0';
		repl_add_line(line);
	}
}

void
repl_eval(void)
{
	char prompt_line[REPL_LINE_LEN];
	s7_pointer result;
	const char *result_str;
	
	if (repl_input_len == 0)
		return;
	
	/* Add the input line with prompt to history */
	snprintf(prompt_line, sizeof(prompt_line), "> %s", repl_input);
	repl_add_line(prompt_line);
	
	/* Evaluate the Scheme expression */
	if (sc) {
		result = s7_eval_c_string(sc, repl_input);
		result_str = s7_object_to_c_string(sc, result);
		if (result_str) {
			/* Split multi-line results */
			char *copy = strdup(result_str);
			char *line = strtok(copy, "\n");
			while (line) {
				repl_add_line(line);
				line = strtok(NULL, "\n");
			}
			free(copy);
			free((void *)result_str);
		}
	} else {
		repl_add_line("Error: Scheme interpreter not initialized");
	}
	
	/* Clear input */
	repl_input[0] = '\0';
	repl_input_len = 0;
	updatebars();
}

int
replkey(xkb_keysym_t sym)
{
	if (sym == XKB_KEY_Escape) {
		repl_input_active = 0;
		repl_input[0] = '\0';
		repl_input_len = 0;
		updatebars();
		return 1;
	}

	if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
		repl_eval();
		return 1;
	}

	if (sym == XKB_KEY_BackSpace) {
		if (repl_input_len > 0) {
			repl_input[--repl_input_len] = '\0';
			updatebars();
		}
		return 1;
	}

	/* Page Up/Down for scrolling history */
	if (sym == XKB_KEY_Page_Up) {
		int visible_lines = sgeom.height / cell_height - 2;
		if (repl_scroll_offset < repl_history_count - visible_lines)
			repl_scroll_offset += visible_lines / 2;
		updaterepl();
		return 1;
	}

	if (sym == XKB_KEY_Page_Down) {
		repl_scroll_offset -= sgeom.height / cell_height / 2;
		if (repl_scroll_offset < 0)
			repl_scroll_offset = 0;
		updaterepl();
		return 1;
	}

	/* Regular character input */
	if (sym >= 0x20 && sym <= 0x7e &&
	    repl_input_len < (int)sizeof(repl_input) - 1) {
		repl_input[repl_input_len++] = (char)sym;
		repl_input[repl_input_len] = '\0';
		updatebars();
		return 1;
	}

	return 1; /* Consume all keys in REPL mode */
}

void
updaterepl(void)
{
	struct TitleBuffer *tb;
	uint32_t *pixels;
	int width, height;
	int x, y, i, line_idx;
	int visible_lines;
	const char *line;
	
	if (!selmon || !layers[LyrBg])
		return;
	
	/* Hide REPL if not visible */
	if (!repl_visible) {
		if (repl_buffer)
			wlr_scene_node_set_enabled(&repl_buffer->node, 0);
		return;
	}
	
	width = sgeom.width;
	height = sgeom.height;
	
	if (width <= 0 || height <= 0)
		return;
	
	/* Create buffer for REPL background */
	tb = ecalloc(1, sizeof(*tb));
	tb->stride = width * 4;
	tb->data = ecalloc(1, tb->stride * height);
	titlebuf_alloc_count++; wlr_buffer_init(&tb->base, &titlebuf_impl, width, height);
	pixels = tb->data;
	
	/* Fill with background color */
	for (i = 0; i < width * height; i++)
		pixels[i] = RGB_TO_ARGB(cfg_bg_color);
	
	/* Calculate visible lines (leave space for bar at top) */
	visible_lines = (height - cell_height) / cell_height;
	
	/* Render history lines from bottom up */
	y = height - cell_height * 2;  /* Start from bottom, leave space for bar */
	
	for (i = 0; i < visible_lines && i + repl_scroll_offset < repl_history_count; i++) {
		line_idx = repl_history_count - 1 - i - repl_scroll_offset;
		if (line_idx < 0)
			break;
		
		line = repl_history[line_idx];
		x = cell_width;
		
		/* Render the line */
		while (*line && x < width - cell_width) {
			render_char_to_buffer(pixels, width, height, x, y, (unsigned char)*line, RGB_TO_ARGB(cfg_bg_text_color));
			x += cell_width;
			line++;
		}
		
		y -= cell_height;
	}
	
	/* Show scroll indicator if not at bottom */
	if (repl_scroll_offset > 0) {
		x = width - cell_width * 3;
		render_char_to_buffer(pixels, width, height, x, height - cell_height * 2, 
			0x25BC, RGB_TO_ARGB(cfg_bg_text_color)); /* ▼ */
	}
	
	/* Create or update the REPL buffer */
	if (!repl_buffer)
		repl_buffer = wlr_scene_buffer_create(layers[LyrBg], NULL);
	wlr_scene_node_set_enabled(&repl_buffer->node, 1);
	wlr_scene_node_set_position(&repl_buffer->node, sgeom.x, sgeom.y);
	wlr_scene_buffer_set_buffer(repl_buffer, &tb->base);
	wlr_buffer_drop(&tb->base);
}

void
updatebar(Monitor *m)
{
	struct TitleBuffer *tb;
	uint32_t *pixels;
	uint32_t bg, fg;
	int width, i, x, tag, n, j;
	int py, px, is_focused, title_max, title_len, date_len, time_len, right_x;
	int len, fits, shown;
	time_t now;
	struct tm *tm_info;
	char timebuf[32], datebuf[32];
	const char *title, *prompt;
	Client *c;
	int max_tab_chars = 20;
	int visible_count = 0;
	int tab_area_width, tab_width_cells;

	if (!m || !m->wlr_output || !m->wlr_output->enabled)
		return;
	
	/* Don't update bar if scene isn't ready */
	if (!layers[LyrOverlay])
		return;

	width = m->m.width;
	if (width <= 0)
		return;

	/* Check if width changed - need to reallocate buffer */
	if (m->bar_buf && width != m->bar_width) {
		if (m->bar)
			wlr_scene_buffer_set_buffer(m->bar, NULL);
		wlr_buffer_drop(&m->bar_buf->base);
		m->bar_buf = NULL;
	}
	m->bar_width = width;

	/* Reuse buffer if available, otherwise allocate new */
	if (!m->bar_buf) {
		m->bar_buf = ecalloc(1, sizeof(*m->bar_buf));
		m->bar_buf->stride = width * 4;
		m->bar_buf->data = ecalloc(1, m->bar_buf->stride * cell_height);
		wlr_buffer_init(&m->bar_buf->base, &titlebuf_impl, width, cell_height);
		titlebuf_alloc_count++;
	}
	tb = m->bar_buf;
	pixels = tb->data;

	/* Fill background */
	for (i = 0; i < width * cell_height; i++)
		pixels[i] = RGB_TO_ARGB(cfg_bar_color);

	x = 0;

	if (repl_input_active) {
		/* === REPL INPUT MODE === */
		prompt = "Scheme> ";
		for (i = 0; prompt[i] && x < width; i++) {
			render_char_to_buffer(pixels, width, cell_height, x, 0, prompt[i], RGB_TO_ARGB(cfg_bar_text_color));
			x += cell_width;
		}
		/* Input text */
		for (i = 0; i < repl_input_len && x < width; i++) {
			render_char_to_buffer(pixels, width, cell_height, x, 0, repl_input[i], RGB_TO_ARGB(cfg_bar_text_color));
			x += cell_width;
		}
		/* Cursor */
		render_char_to_buffer(pixels, width, cell_height, x, 0, '_', RGB_TO_ARGB(cfg_bar_text_color));
	} else if (launcher_active) {
		/* === LAUNCHER MODE === */
		prompt = "Launcher> ";
		for (i = 0; prompt[i] && x < width; i++) {
			render_char_to_buffer(pixels, width, cell_height, x, 0, prompt[i], RGB_TO_ARGB(cfg_bar_text_color));
			x += cell_width;
		}
		/* Input text */
		for (i = 0; i < launcher_input_len && x < width; i++) {
			render_char_to_buffer(pixels, width, cell_height, x, 0, launcher_input[i], RGB_TO_ARGB(cfg_bar_text_color));
			x += cell_width;
		}
		/* Separator */
		x += cell_width;
		render_char_to_buffer(pixels, width, cell_height, x, 0, '|', RGB_TO_ARGB(cfg_bar_text_color));
		x += cell_width * 2;

		/* Suggestions - only if there's input */
		if (launcher_input_len > 0) {
			shown = 0;
			for (i = 0; i < app_cache_count && x < width - cell_width; i++) {
				/* Prefix match */
				if (strncmp(app_cache[i], launcher_input, launcher_input_len) == 0) {
					len = strlen(app_cache[i]);
					fits = (x + (len + 3) * cell_width) <= width;
					if (!fits && shown > 0)
						break;

					/* Highlight selected suggestion */
					if (shown == launcher_selection) {
						bg = RGB_TO_ARGB(cfg_bar_text_color);
						fg = RGB_TO_ARGB(cfg_bar_color);
						/* Draw background */
						for (py = 0; py < cell_height; py++) {
							for (px = x; px < x + (len + 2) * cell_width && px < width; px++) {
								pixels[py * width + px] = bg;
							}
						}
					} else {
						fg = RGB_TO_ARGB(cfg_bar_text_color);
					}

					render_char_to_buffer(pixels, width, cell_height, x, 0, '[', fg);
					x += cell_width;
					for (j = 0; app_cache[i][j] && x < width - cell_width * 2; j++) {
						render_char_to_buffer(pixels, width, cell_height, x, 0, app_cache[i][j], fg);
						x += cell_width;
					}
					render_char_to_buffer(pixels, width, cell_height, x, 0, ']', fg);
					x += cell_width * 2;
					shown++;
				}
			}
		}
	} else {
		/* === STATUS BAR MODE === */
		/* App menu button [X] */
		{
			int btn_len = strlen(cfg_menu_button);
			int btn_cells = btn_len + 2; /* [btn] */
			if (appmenu_active) {
				bg = RGB_TO_ARGB(cfg_bar_text_color);
				fg = RGB_TO_ARGB(cfg_bar_color);
				for (py = 0; py < cell_height; py++) {
					for (px = x; px < x + btn_cells * cell_width && px < width; px++) {
						pixels[py * width + px] = bg;
					}
				}
			} else {
				fg = RGB_TO_ARGB(cfg_bar_text_color);
			}
		}
		render_char_to_buffer(pixels, width, cell_height, x, 0, '[', fg);
		x += cell_width;
		{
			int bi;
			for (bi = 0; cfg_menu_button[bi] && bi < 14; bi++) {
				render_char_to_buffer(pixels, width, cell_height, x, 0, cfg_menu_button[bi], fg);
				x += cell_width;
			}
		}
		render_char_to_buffer(pixels, width, cell_height, x, 0, ']', fg);
		x += cell_width;
		
		/* Separator */
		x += cell_width / 2;
		render_char_to_buffer(pixels, width, cell_height, x, 0, '|', RGB_TO_ARGB(cfg_bar_text_color));
		x += cell_width + cell_width / 2;

		/* Tags [1] [2] [3] ... - use cfg_tagcount */
		for (tag = 0; tag < cfg_tagcount; tag++) {
			bg = RGB_TO_ARGB(cfg_bar_color);
			fg = RGB_TO_ARGB(cfg_bar_text_color);

			/* Highlight selected tag */
			if (m->tagset[m->seltags] & (1 << tag)) {
				bg = RGB_TO_ARGB(cfg_bar_text_color);
				fg = RGB_TO_ARGB(cfg_bar_color);
			}

			/* Draw tag background if highlighted */
			if (bg != RGB_TO_ARGB(cfg_bar_color)) {
				for (py = 0; py < cell_height; py++) {
					for (px = x; px < x + 3 * cell_width && px < width; px++) {
						pixels[py * width + px] = bg;
					}
				}
			}

			render_char_to_buffer(pixels, width, cell_height, x, 0, '[', fg);
			x += cell_width;
			render_char_to_buffer(pixels, width, cell_height, x, 0, '1' + tag, fg);
			x += cell_width;
			render_char_to_buffer(pixels, width, cell_height, x, 0, ']', fg);
			x += cell_width;
			x += cell_width / 2; /* Small gap */
		}

		/* Separator */
		x += cell_width / 2;
		render_char_to_buffer(pixels, width, cell_height, x, 0, '|', RGB_TO_ARGB(cfg_bar_text_color));
		x += cell_width * 2;

		/* Window tabs */
		visible_count = 0;
		wl_list_for_each(c, &clients, link) {
			if (VISIBLEON(c, m))
				visible_count++;
		}

		if (visible_count > 0) {
			/* Calculate how much space we have for tabs */
			/* Reserve space for: | date | time at the end */
			n = 30 * cell_width; /* reserved */
			tab_area_width = width - x - n;
			if (tab_area_width < 0) tab_area_width = 0;

			/* Shrink tab width if needed */
			tab_width_cells = max_tab_chars + 2; /* +2 for brackets */
			if (visible_count * tab_width_cells * cell_width > tab_area_width) {
				tab_width_cells = tab_area_width / (visible_count * cell_width);
				if (tab_width_cells < 5) tab_width_cells = 5;
			}

			wl_list_for_each(c, &clients, link) {
				if (!VISIBLEON(c, m))
					continue;
				if (x >= width - n)
					break;

				title = client_get_title(c);
				if (!title) title = "?";

				is_focused = (c == focustop(m));
				bg = RGB_TO_ARGB(cfg_bar_color);
				fg = RGB_TO_ARGB(cfg_bar_text_color);

				if (is_focused) {
					bg = RGB_TO_ARGB(cfg_bar_text_color);
					fg = RGB_TO_ARGB(cfg_bar_color);
				}

				title_max = tab_width_cells - 2;
				title_len = strlen(title);
				
				/* Calculate actual tab width for background */
				int actual_title_chars = (title_len < title_max - 1) ? title_len : title_max - 1;
				int actual_tab_width = (actual_title_chars + 2) * cell_width; /* +2 for brackets */
				
				/* Draw background for focused */
				if (is_focused) {
					for (py = 0; py < cell_height; py++) {
						for (px = x; px < x + actual_tab_width && px < width; px++) {
							pixels[py * width + px] = bg;
						}
					}
				}

				render_char_to_buffer(pixels, width, cell_height, x, 0, '[', fg);
				x += cell_width;
				
				if (title_len > title_max - 1 && title_scroll_mode) {
					/* Smooth pixel-based scrolling for top bar tabs */
					/* First decode title to codepoints */
					unsigned long title_cps[256];
					int title_cp_count = 0, utf8_pos = 0;
					while (title[utf8_pos] && title_cp_count < 255) {
						title_cps[title_cp_count++] = utf8_decode(title, &utf8_pos);
					}
					
					int scroll_chars = title_cp_count + 2; /* +2 for "  " separator */
					int total_scroll_width = scroll_chars * cell_width;
					int pixel_offset = title_scroll_offset % total_scroll_width;
					int display_width = (title_max - 1) * cell_width;
					int text_start_x = x;
					int text_end_x = x + display_width;
					int char_idx;
					
					any_title_needs_scroll = 1;
					
					for (char_idx = 0; char_idx < scroll_chars + title_max; char_idx++) {
						int src_char = char_idx % scroll_chars;
						int draw_x = text_start_x + char_idx * cell_width - pixel_offset;
						unsigned long cp;
						
						cp = (src_char < title_cp_count) ? title_cps[src_char] : ' ';
						render_char_clipped(pixels, width, cell_height, draw_x, 0, cp, fg, text_start_x, text_end_x);
					}
					x += display_width;
				} else if (title_len > title_max - 1 && title_max > 3) {
					/* Truncation mode with ellipsis */
					int utf8_pos = 0, char_count = 0;
					while (title[utf8_pos] && char_count < title_max - 4 && x < width - cell_width) {
						unsigned long cp = utf8_decode(title, &utf8_pos);
						render_char_to_buffer(pixels, width, cell_height, x, 0, cp, fg);
						x += cell_width;
						char_count++;
					}
					render_char_to_buffer(pixels, width, cell_height, x, 0, '.', fg);
					x += cell_width;
					render_char_to_buffer(pixels, width, cell_height, x, 0, '.', fg);
					x += cell_width;
					render_char_to_buffer(pixels, width, cell_height, x, 0, '.', fg);
					x += cell_width;
				} else {
					/* Title fits */
					int utf8_pos = 0, char_count = 0;
					while (title[utf8_pos] && char_count < title_max - 1 && x < width - cell_width) {
						unsigned long cp = utf8_decode(title, &utf8_pos);
						render_char_to_buffer(pixels, width, cell_height, x, 0, cp, fg);
						x += cell_width;
						char_count++;
					}
				}

				render_char_to_buffer(pixels, width, cell_height, x, 0, ']', fg);
				x += cell_width;
				x += cell_width / 2; /* Gap between tabs */
			}
		}

		/* Right-align status: custom text OR date/time */
		now = time(NULL);
		tm_info = localtime(&now);
		
		/* Check if custom status text is set */
		if (cfg_status_text[0] != '\0') {
			/* Custom status text mode */
			int status_len = strlen(cfg_status_text);
			right_x = width - (status_len + 3) * cell_width;
			
			if (right_x > x) {
				x = right_x;
				render_char_to_buffer(pixels, width, cell_height, x, 0, '|', RGB_TO_ARGB(cfg_bar_text_color));
				x += cell_width * 2;
				for (i = 0; cfg_status_text[i]; i++) {
					render_char_to_buffer(pixels, width, cell_height, x, 0, (unsigned char)cfg_status_text[i], RGB_TO_ARGB(cfg_bar_text_color));
					x += cell_width;
				}
			}
		} else if (cfg_show_date || cfg_show_time) {
			/* Date/time mode */
			strftime(datebuf, sizeof(datebuf), "%Y-%m-%d", tm_info);
			strftime(timebuf, sizeof(timebuf), "%I:%M:%S %p", tm_info);

			date_len = cfg_show_date ? strlen(datebuf) : 0;
			time_len = cfg_show_time ? strlen(timebuf) : 0;
			
			/* Calculate right-aligned position */
			int total_chars = 0;
			if (cfg_show_date && cfg_show_time) {
				total_chars = date_len + time_len + 5; /* | date | time */
			} else if (cfg_show_date) {
				total_chars = date_len + 3; /* | date */
			} else if (cfg_show_time) {
				total_chars = time_len + 3; /* | time */
			}
			right_x = width - total_chars * cell_width;

			if (right_x > x) {
				x = right_x;
				render_char_to_buffer(pixels, width, cell_height, x, 0, '|', RGB_TO_ARGB(cfg_bar_text_color));
				x += cell_width * 2;
				
				if (cfg_show_date) {
					for (i = 0; datebuf[i]; i++) {
						render_char_to_buffer(pixels, width, cell_height, x, 0, datebuf[i], RGB_TO_ARGB(cfg_bar_text_color));
						x += cell_width;
					}
					if (cfg_show_time) {
						x += cell_width;
						render_char_to_buffer(pixels, width, cell_height, x, 0, '|', RGB_TO_ARGB(cfg_bar_text_color));
						x += cell_width * 2;
					}
				}
				
				if (cfg_show_time) {
					for (i = 0; timebuf[i]; i++) {
						render_char_to_buffer(pixels, width, cell_height, x, 0, timebuf[i], RGB_TO_ARGB(cfg_bar_text_color));
						x += cell_width;
					}
				}
			}
		}
	}

	/* Set the bar buffer */
	if (!m->bar)
		m->bar = wlr_scene_buffer_create(layers[LyrTop], NULL);
	wlr_scene_node_set_position(&m->bar->node, m->m.x, m->m.y);
	wlr_scene_buffer_set_buffer(m->bar, &tb->base);
	/* Don't drop - we're caching the buffer for reuse */
}

/* Check if there's an adjacent tiled window in the given direction.
 * Windows overlap by 1 cell when tiled, so we check if our top row
 * is covered by another window's bottom row, etc. */
static int
has_neighbor(Client *c, int dir) /* 0=above, 1=below, 2=left, 3=right */
{
	Client *other;
	int h_overlap, v_overlap;

	if (!c || !c->mon)
		return 0;
	
	/* Floating windows never have neighbors (always draw full border) */
	if (c->isfloating)
		return 0;
	
	wl_list_for_each(other, &clients, link) {
		if (other == c || other->mon != c->mon)
			continue;
		if (!VISIBLEON(other, c->mon) || other->isfloating || other->isfullscreen)
			continue;
		
		/* Check horizontal overlap for vertical neighbors */
		h_overlap = (other->geom.x < c->geom.x + c->geom.width &&
		             other->geom.x + other->geom.width > c->geom.x);
		/* Check vertical overlap for horizontal neighbors */
		v_overlap = (other->geom.y < c->geom.y + c->geom.height &&
		             other->geom.y + other->geom.height > c->geom.y);
		
		switch (dir) {
		case 0: /* above: other overlaps our top row */
			/* other's bottom row overlaps our top row */
			if (h_overlap &&
			    other->geom.y < c->geom.y &&
			    other->geom.y + other->geom.height > c->geom.y)
				return 1;
			break;
		case 1: /* below: other overlaps our bottom row */
			/* other's top row overlaps our bottom row */
			if (h_overlap &&
			    other->geom.y < c->geom.y + c->geom.height &&
			    other->geom.y + other->geom.height > c->geom.y + c->geom.height)
				return 1;
			break;
		case 2: /* left: other overlaps our left column */
			if (v_overlap &&
			    other->geom.x < c->geom.x &&
			    other->geom.x + other->geom.width > c->geom.x)
				return 1;
			break;
		case 3: /* right: other overlaps our right column */
			if (v_overlap &&
			    other->geom.x < c->geom.x + c->geom.width &&
			    other->geom.x + other->geom.width > c->geom.x + c->geom.width)
				return 1;
			break;
		}
	}
	return 0;
}

/* Decode one UTF-8 character, return codepoint and advance *pos */
static unsigned long
utf8_decode(const char *s, int *pos)
{
	unsigned char c = s[*pos];
	unsigned long cp;
	int bytes;

	if ((c & 0x80) == 0) {
		(*pos)++;
		return c;
	} else if ((c & 0xE0) == 0xC0) {
		bytes = 2;
		cp = c & 0x1F;
	} else if ((c & 0xF0) == 0xE0) {
		bytes = 3;
		cp = c & 0x0F;
	} else if ((c & 0xF8) == 0xF0) {
		bytes = 4;
		cp = c & 0x07;
	} else {
		(*pos)++;
		return '?';
	}

	for (int i = 1; i < bytes; i++) {
		if ((s[*pos + i] & 0xC0) != 0x80) {
			(*pos)++;
			return '?';
		}
		cp = (cp << 6) | (s[*pos + i] & 0x3F);
	}
	*pos += bytes;
	return cp;
}

/* Get a cached glyph, loading and caching if necessary */
static CachedGlyph *
get_cached_glyph(unsigned long charcode)
{
	int start_idx = charcode % GLYPH_CACHE_SIZE;
	int idx = start_idx;
	int empty_slot = -1;
	CachedGlyph *cg;
	FT_GlyphSlot slot;
	int size;

	/* Linear probing to find existing entry or empty slot */
	do {
		cg = &glyph_cache[idx];
		if (!cg->valid) {
			/* Found an empty slot - remember it for insertion */
			if (empty_slot < 0)
				empty_slot = idx;
		} else if (cg->charcode == charcode) {
			/* Found cached glyph */
			return cg;
		}
		idx = (idx + 1) % GLYPH_CACHE_SIZE;
	} while (idx != start_idx);

	/* Not found - use empty slot or evict at start position */
	idx = (empty_slot >= 0) ? empty_slot : start_idx;
	cg = &glyph_cache[idx];

	/* Need to load this glyph */
	if (FT_Load_Char(ft_face, charcode, FT_LOAD_RENDER)) {
		if (charcode != '?' && FT_Load_Char(ft_face, '?', FT_LOAD_RENDER))
			return NULL;
	}

	slot = ft_face->glyph;

	/* Free old bitmap if present */
	if (cg->bitmap) {
		free(cg->bitmap);
		cg->bitmap = NULL;
		glyph_free_count++;
	}

	/* Cache the glyph data */
	cg->charcode = charcode;
	cg->width = slot->bitmap.width;
	cg->rows = slot->bitmap.rows;
	cg->pitch = slot->bitmap.pitch;
	cg->bitmap_left = slot->bitmap_left;
	cg->bitmap_top = slot->bitmap_top;

	/* Copy the bitmap */
	size = cg->pitch * cg->rows;
	if (size > 0) {
		cg->bitmap = malloc(size);
		glyph_malloc_count++;
		glyph_total_bytes += size;
		if (cg->bitmap)
			memcpy(cg->bitmap, slot->bitmap.buffer, size);
	}

	cg->valid = 1;
	return cg;
}

void
render_char_to_buffer(uint32_t *pixels, int buf_w, int buf_h, int x, int y,
                      unsigned long charcode, uint32_t color)
{
	CachedGlyph *cg;
	int gx, gy, px, py;
	unsigned char v, r, g, b;

	cg = get_cached_glyph(charcode);
	if (!cg || !cg->bitmap)
		return;

	r = (color >> 16) & 0xFF;
	g = (color >> 8) & 0xFF;
	b = color & 0xFF;

	for (gy = 0; gy < cg->rows; gy++) {
		/* Align to top of cell - ascender at top, descender may clip at bottom */
		py = y + (cell_height - cg->bitmap_top) + gy - 4;
		if (py < 0 || py >= buf_h) continue;
		for (gx = 0; gx < cg->width; gx++) {
			px = x + cg->bitmap_left + gx;
			if (px < 0 || px >= buf_w) continue;
			v = cg->bitmap[gy * cg->pitch + gx];
			if (v > 0)
				pixels[py * buf_w + px] = 0xFF000000 |
					((r * v / 255) << 16) |
					((g * v / 255) << 8) |
					(b * v / 255);
		}
	}
}

/* Render character with horizontal clipping bounds */
void
render_char_clipped(uint32_t *pixels, int buf_w, int buf_h, int x, int y,
                    unsigned long charcode, uint32_t color, int clip_left, int clip_right)
{
	CachedGlyph *cg;
	int gx, gy, px, py;
	unsigned char v, r, g, b;

	cg = get_cached_glyph(charcode);
	if (!cg || !cg->bitmap)
		return;

	r = (color >> 16) & 0xFF;
	g = (color >> 8) & 0xFF;
	b = color & 0xFF;

	for (gy = 0; gy < cg->rows; gy++) {
		py = y + (cell_height - cg->bitmap_top) + gy - 4;
		if (py < 0 || py >= buf_h) continue;
		for (gx = 0; gx < cg->width; gx++) {
			px = x + cg->bitmap_left + gx;
			if (px < clip_left || px >= clip_right) continue;
			if (px < 0 || px >= buf_w) continue;
			v = cg->bitmap[gy * cg->pitch + gx];
			if (v > 0)
				pixels[py * buf_w + px] = 0xFF000000 |
					((r * v / 255) << 16) |
					((g * v / 255) << 8) |
					(b * v / 255);
		}
	}
}

void
updateframe(Client *c)
{
	const char *title;
	int width, height, i, x;
	int above, below, left, right;
	int focused;
	int title_len;
	unsigned long tl_char, tr_char, bl_char, br_char;
	unsigned long h_line, v_line;
	uint32_t bg_color;
	struct TitleBuffer *tb;
	uint32_t *pixels;
	int dims_changed;

	if (!c || !c->mon)
		return;

	/* Hide frames when fullscreen */
	if (c->isfullscreen) {
		if (c->frame_top)
			wlr_scene_buffer_set_buffer(c->frame_top, NULL);
		if (c->frame_bottom)
			wlr_scene_buffer_set_buffer(c->frame_bottom, NULL);
		if (c->frame_left)
			wlr_scene_buffer_set_buffer(c->frame_left, NULL);
		if (c->frame_right)
			wlr_scene_buffer_set_buffer(c->frame_right, NULL);
		return;
	}

	title = client_get_title(c);
	if (!title)
		title = "untitled";

	width = c->geom.width;
	height = c->geom.height;

	if (width <= 0 || height <= 0)
		return;

	/* Check if dimensions changed - if so, we need to reallocate buffers */
	dims_changed = (width != c->frame_width || height != c->frame_height);
	if (dims_changed) {
		/* Release old cached buffers properly - scene must release first, then we drop our ref */
		if (c->frame_top_buf) {
			if (c->frame_top)
				wlr_scene_buffer_set_buffer(c->frame_top, NULL);
			wlr_buffer_drop(&c->frame_top_buf->base);
			c->frame_top_buf = NULL;
		}
		if (c->frame_bottom_buf) {
			if (c->frame_bottom)
				wlr_scene_buffer_set_buffer(c->frame_bottom, NULL);
			wlr_buffer_drop(&c->frame_bottom_buf->base);
			c->frame_bottom_buf = NULL;
		}
		if (c->frame_left_buf) {
			if (c->frame_left)
				wlr_scene_buffer_set_buffer(c->frame_left, NULL);
			wlr_buffer_drop(&c->frame_left_buf->base);
			c->frame_left_buf = NULL;
		}
		if (c->frame_right_buf) {
			if (c->frame_right)
				wlr_scene_buffer_set_buffer(c->frame_right, NULL);
			wlr_buffer_drop(&c->frame_right_buf->base);
			c->frame_right_buf = NULL;
		}
		c->frame_width = width;
		c->frame_height = height;
	}

	/* Check if this window is focused */
	focused = (focustop(c->mon) == c);

	/* Check for neighbors */
	above = has_neighbor(c, 0);
	below = has_neighbor(c, 1);
	left  = has_neighbor(c, 2);
	right = has_neighbor(c, 3);

	/* Always use double-line characters */
	h_line = 0x2550; /* ═ double horizontal */
	v_line = 0x2551; /* ║ double vertical */
	
	/* Double-line corners */
	if (above && left)       tl_char = 0x256C; /* ╬ */
	else if (above)          tl_char = 0x2560; /* ╠ */
	else if (left)           tl_char = 0x2566; /* ╦ */
	else                     tl_char = 0x2554; /* ╔ */

	if (above && right)      tr_char = 0x256C; /* ╬ */
	else if (above)          tr_char = 0x2563; /* ╣ */
	else if (right)          tr_char = 0x2566; /* ╦ */
	else                     tr_char = 0x2557; /* ╗ */

	if (below && left)       bl_char = 0x256C; /* ╬ */
	else if (below)          bl_char = 0x2560; /* ╠ */
	else if (left)           bl_char = 0x2569; /* ╩ */
	else                     bl_char = 0x255A; /* ╚ */

	if (below && right)      br_char = 0x256C; /* ╬ */
	else if (below)          br_char = 0x2563; /* ╣ */
	else if (right)          br_char = 0x2569; /* ╩ */
	else                     br_char = 0x255D; /* ╝ */

	/* All windows use blue background */
	bg_color = RGB_TO_ARGB(cfg_border_color);

	/* === TOP FRAME === */
	/* Reuse buffer if dimensions match, otherwise allocate new */
	if (!c->frame_top_buf) {
		c->frame_top_buf = ecalloc(1, sizeof(*c->frame_top_buf));
		c->frame_top_buf->stride = width * 4;
		c->frame_top_buf->data = ecalloc(1, c->frame_top_buf->stride * cell_height);
		wlr_buffer_init(&c->frame_top_buf->base, &titlebuf_impl, width, cell_height);
		titlebuf_alloc_count++;
	}
	tb = c->frame_top_buf;
	pixels = tb->data;
	
	/* Clear buffer */
	for (i = 0; i < width * cell_height; i++)
		pixels[i] = bg_color;

	/* Format: ╔═ title ═╗ with horizontal lines filling the rest */
	render_char_to_buffer(pixels, width, cell_height, 0, 0, tl_char, RGB_TO_ARGB(cfg_border_line_color));
	render_char_to_buffer(pixels, width, cell_height, cell_width, 0, h_line, RGB_TO_ARGB(cfg_border_line_color));
	
	/* Calculate title positioning */
	title_len = 0;
	while (title[title_len]) title_len++;
	
	/* Title area: from cell 2 to width - cell*2, but we need space for " title " */
	/* Available cells for title content: (width/cell_width) - 4 corners/lines - 2 spaces = width/cell - 6 */
	{
		int avail_cells = (width / cell_width) - 6;
		int title_cells = title_len;
		int needs_overflow = (title_cells > avail_cells);
		int title_start_cell, title_x, fill_x, py, px;
		uint32_t title_bg, title_fg;
		
		/* Update scroll flag for this client */
		c->needs_title_scroll = (needs_overflow && title_scroll_mode) ? 1 : 0;
		if (c->needs_title_scroll)
			any_title_needs_scroll = 1;
		
		/* Fill cells 2 to (width/cell - 2) with h_line first */
		for (fill_x = cell_width * 2; fill_x < width - cell_width * 2; fill_x += cell_width) {
			render_char_to_buffer(pixels, width, cell_height, fill_x, 0, h_line, RGB_TO_ARGB(cfg_border_line_color));
		}
		
		/* Title colors: inverted only for focused window */
		if (focused) {
			title_bg = RGB_TO_ARGB(cfg_border_line_color);  /* gray background */
			title_fg = bg_color;            /* blue text */
		} else {
			title_bg = bg_color;            /* blue background */
			title_fg = RGB_TO_ARGB(cfg_border_line_color);  /* gray text */
		}
		
		/* Title starts at cell 2 */
		title_start_cell = 2;
		title_x = title_start_cell * cell_width;
		
		if (needs_overflow && title_scroll_mode) {
			/* Smooth pixel-based scrolling: title + "  " loops around */
			int scroll_chars = title_len + 2; /* +2 for "  " separator */
			int total_scroll_width = scroll_chars * cell_width;
			int pixel_offset = title_scroll_offset % total_scroll_width;
			int display_width = avail_cells * cell_width;
			int bg_end = title_x + display_width;
			if (bg_end > width - cell_width * 2)
				bg_end = width - cell_width * 2;
			
			/* Fill background for display area */
			for (py = 0; py < cell_height; py++) {
				for (px = title_x; px < bg_end; px++) {
					pixels[py * width + px] = title_bg;
				}
			}
			
			/* Render characters at pixel-offset positions with clipping */
			{
				int char_idx;
				int draw_x;
				int clip_left = title_x;
				int clip_right = bg_end;
				for (char_idx = 0; char_idx < scroll_chars + avail_cells; char_idx++) {
					int src_char = char_idx % scroll_chars;
					int char_pixel_pos = char_idx * cell_width - pixel_offset;
					unsigned long cp;
					draw_x = title_x + char_pixel_pos;
					
					if (src_char < title_len) {
						/* Get character from title */
						int pos = 0;
						int ci = 0;
						while (ci < src_char && title[pos]) {
							utf8_decode(title, &pos);
							ci++;
						}
						cp = title[pos] ? utf8_decode(title, &pos) : ' ';
					} else {
						cp = ' '; /* separator spaces */
					}
					render_char_clipped(pixels, width, cell_height, draw_x, 0, cp, title_fg, clip_left, clip_right);
				}
			}
		} else if (needs_overflow) {
			/* Truncate mode with ellipsis */
			int ellipsis_cells = 3;
			int text_cells = avail_cells - ellipsis_cells;
			if (text_cells < 0) text_cells = 0;
			
			/* Fill background */
			{
				int bg_start = title_x;
				int bg_end = title_x + avail_cells * cell_width;
				if (bg_end > width - cell_width * 2)
					bg_end = width - cell_width * 2;
				for (py = 0; py < cell_height; py++) {
					for (px = bg_start; px < bg_end; px++) {
						pixels[py * width + px] = title_bg;
					}
				}
			}
			
			/* Render title text */
			{
				int pos = 0;
				int rendered = 0;
				while (rendered < text_cells && title[pos]) {
					unsigned long cp = utf8_decode(title, &pos);
					render_char_to_buffer(pixels, width, cell_height, title_x, 0, cp, title_fg);
					title_x += cell_width;
					rendered++;
				}
			}
			
			/* Ellipsis */
			render_char_to_buffer(pixels, width, cell_height, title_x, 0, '.', title_fg);
			title_x += cell_width;
			render_char_to_buffer(pixels, width, cell_height, title_x, 0, '.', title_fg);
			title_x += cell_width;
			render_char_to_buffer(pixels, width, cell_height, title_x, 0, '.', title_fg);
		} else {
			/* Title fits - render normally */
			/* Fill background */
			{
				int bg_start = title_x;
				int bg_end = title_x + title_cells * cell_width;
				if (bg_end > width - cell_width * 2)
					bg_end = width - cell_width * 2;
				for (py = 0; py < cell_height; py++) {
					for (px = bg_start; px < bg_end; px++) {
						pixels[py * width + px] = title_bg;
					}
				}
			}
			
			/* Render title */
			{
				int pos = 0;
				int rendered = 0;
				while (rendered < title_cells && title[pos]) {
					unsigned long cp = utf8_decode(title, &pos);
					render_char_to_buffer(pixels, width, cell_height, title_x, 0, cp, title_fg);
					title_x += cell_width;
					rendered++;
				}
			}
		}
	}
	
	/* End corner */
	render_char_to_buffer(pixels, width, cell_height, width - cell_width * 2, 0, h_line, RGB_TO_ARGB(cfg_border_line_color));
	render_char_to_buffer(pixels, width, cell_height, width - cell_width, 0, tr_char, RGB_TO_ARGB(cfg_border_line_color));

	if (!c->frame_top)
		c->frame_top = wlr_scene_buffer_create(c->scene, NULL);
	wlr_scene_node_set_position(&c->frame_top->node, 0, 0);
	wlr_scene_buffer_set_buffer(c->frame_top, &tb->base);
	/* Don't drop - we're caching the buffer for reuse */

	/* === BOTTOM FRAME === */
	/* Only draw if no neighbor below (neighbor draws the shared border) */
	if (!below) {
		/* Reuse buffer if dimensions match */
		if (!c->frame_bottom_buf) {
			c->frame_bottom_buf = ecalloc(1, sizeof(*c->frame_bottom_buf));
			c->frame_bottom_buf->stride = width * 4;
			c->frame_bottom_buf->data = ecalloc(1, c->frame_bottom_buf->stride * cell_height);
			wlr_buffer_init(&c->frame_bottom_buf->base, &titlebuf_impl, width, cell_height);
			titlebuf_alloc_count++;
		}
		tb = c->frame_bottom_buf;
		pixels = tb->data;
		
		/* Clear buffer */
		for (i = 0; i < width * cell_height; i++)
			pixels[i] = bg_color;

		render_char_to_buffer(pixels, width, cell_height, 0, 0, bl_char, RGB_TO_ARGB(cfg_border_line_color));
		
		for (x = cell_width; x < width - cell_width; x += cell_width)
			render_char_to_buffer(pixels, width, cell_height, x, 0, h_line, RGB_TO_ARGB(cfg_border_line_color));
		
		render_char_to_buffer(pixels, width, cell_height, width - cell_width, 0, br_char, RGB_TO_ARGB(cfg_border_line_color));

		if (!c->frame_bottom)
			c->frame_bottom = wlr_scene_buffer_create(c->scene, NULL);
		wlr_scene_node_set_position(&c->frame_bottom->node, 0, height - cell_height);
		wlr_scene_buffer_set_buffer(c->frame_bottom, &tb->base);
		/* Don't drop - we're caching the buffer for reuse */
	} else if (c->frame_bottom) {
		wlr_scene_buffer_set_buffer(c->frame_bottom, NULL);
	}

	/* === LEFT FRAME === */
	/* Always draw for focused windows (so focus color shows), otherwise only if no neighbor */
	if (focused || !left) {
		int side_height = height - 2 * cell_height;
		int rows = side_height / cell_height;
		
		if (rows > 0) {
			/* Reuse buffer if dimensions match */
			if (!c->frame_left_buf) {
				c->frame_left_buf = ecalloc(1, sizeof(*c->frame_left_buf));
				c->frame_left_buf->stride = cell_width * 4;
				c->frame_left_buf->data = ecalloc(1, c->frame_left_buf->stride * side_height);
				wlr_buffer_init(&c->frame_left_buf->base, &titlebuf_impl, cell_width, side_height);
				titlebuf_alloc_count++;
			}
			tb = c->frame_left_buf;
			pixels = tb->data;
			
			/* Clear buffer */
			for (i = 0; i < cell_width * side_height; i++)
				pixels[i] = bg_color;

			for (i = 0; i < rows; i++)
				render_char_to_buffer(pixels, cell_width, side_height, 0, i * cell_height, v_line, RGB_TO_ARGB(cfg_border_line_color));

			if (!c->frame_left)
				c->frame_left = wlr_scene_buffer_create(c->scene, NULL);
			wlr_scene_node_set_position(&c->frame_left->node, 0, cell_height);
			wlr_scene_buffer_set_buffer(c->frame_left, &tb->base);
			/* Don't drop - we're caching the buffer for reuse */
		}
	} else if (c->frame_left) {
		wlr_scene_buffer_set_buffer(c->frame_left, NULL);
	}

	/* === RIGHT FRAME === */
	/* Always draw right border - neighbor to right will skip its left border */
	{
		int side_height = height - 2 * cell_height;
		int rows = side_height / cell_height;
		
		if (rows > 0) {
			/* Reuse buffer if dimensions match */
			if (!c->frame_right_buf) {
				c->frame_right_buf = ecalloc(1, sizeof(*c->frame_right_buf));
				c->frame_right_buf->stride = cell_width * 4;
				c->frame_right_buf->data = ecalloc(1, c->frame_right_buf->stride * side_height);
				wlr_buffer_init(&c->frame_right_buf->base, &titlebuf_impl, cell_width, side_height);
				titlebuf_alloc_count++;
			}
			tb = c->frame_right_buf;
			pixels = tb->data;
			
			/* Clear buffer */
			for (i = 0; i < cell_width * side_height; i++)
				pixels[i] = bg_color;

			for (i = 0; i < rows; i++)
				render_char_to_buffer(pixels, cell_width, side_height, 0, i * cell_height, v_line, RGB_TO_ARGB(cfg_border_line_color));

			if (!c->frame_right)
				c->frame_right = wlr_scene_buffer_create(c->scene, NULL);
			wlr_scene_node_set_position(&c->frame_right->node, width - cell_width, cell_height);
			wlr_scene_buffer_set_buffer(c->frame_right, &tb->base);
			/* Don't drop - we're caching the buffer for reuse */
		} else if (c->frame_right) {
			wlr_scene_buffer_set_buffer(c->frame_right, NULL);
		}
	}
}

void
startdrag(struct wl_listener *listener, void *data)
{
	struct wlr_drag *drag = data;
	if (!drag->icon)
		return;

	drag->icon->data = &wlr_scene_drag_icon_create(drag_icon, drag->icon)->node;
	LISTEN_STATIC(&drag->icon->events.destroy, destroydragicon);
}

void
tag(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (!sel || (arg->ui & TAGMASK) == 0)
		return;

	/* Remove from current dwindle tree before changing tags */
	if (sel->dwindle) {
		dwindle_remove(sel);
		sel->dwindle = NULL;
	}
	
	sel->tags = arg->ui & TAGMASK;
	focusclient(focustop(selmon), 1);
	arrange(selmon);
	printstatus();
}

void
tagmon(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		setmon(sel, dirtomon(arg->i), 0);
}

void
tile(Monitor *m)
{
	unsigned int mw, my, ty;
	int i, n = 0, n_master = 0, n_stack = 0;
	int total_cells_w, total_cells_h, master_cells_w, stack_cells_w;
	int stack_x, stack_w;
	Client *c;

	wl_list_for_each(c, &clients, link)
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen)
			n++;
	if (n == 0)
		return;

	/* Count windows in master and stack */
	n_master = MIN(n, m->nmaster);
	n_stack = n - n_master;

	/* Calculate grid-aligned dimensions */
	total_cells_w = m->w.width / cell_width;
	total_cells_h = m->w.height / cell_height;

	if (n > m->nmaster)
		master_cells_w = m->nmaster ? (int)(total_cells_w * m->mfact) : 0;
	else
		master_cells_w = total_cells_w;

	mw = master_cells_w * cell_width;
	
	/* Stack overlaps master by 1 cell horizontally for shared border */
	stack_cells_w = total_cells_w - master_cells_w + (n_stack > 0 && n_master > 0 ? 1 : 0);
	stack_x = m->w.x + mw - (n_stack > 0 && n_master > 0 ? cell_width : 0);
	stack_w = stack_cells_w * cell_width;
	
	i = my = ty = 0;

	wl_list_for_each(c, &clients, link) {
		int h_cells, h, remaining;
		int is_first;
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
			continue;
		
		if (i < m->nmaster) {
			is_first = (i == 0);
			remaining = n_master - i;
			
			/* Distribute height: total_cells_h among remaining windows */
			/* Windows after first overlap by 1 cell with window above */
			if (is_first) {
				h_cells = (total_cells_h + (remaining - 1)) / remaining;
			} else {
				int cells_used = my / cell_height;
				int cells_left = total_cells_h - cells_used + 1; /* +1 for overlap */
				h_cells = (cells_left + (remaining - 1)) / remaining;
			}
			h = h_cells * cell_height;
			
			resize(c, (struct wlr_box){
				.x = m->w.x,
				.y = m->w.y + my,
				.width = mw,
				.height = h
			}, 0);
			
			/* Next window overlaps by 1 cell (shared border) */
			my += h - cell_height;
		} else {
			int stack_i = i - m->nmaster;
			is_first = (stack_i == 0);
			remaining = n_stack - stack_i;
			
			if (is_first) {
				h_cells = (total_cells_h + (remaining - 1)) / remaining;
			} else {
				int cells_used = ty / cell_height;
				int cells_left = total_cells_h - cells_used + 1;
				h_cells = (cells_left + (remaining - 1)) / remaining;
			}
			h = h_cells * cell_height;
			
			resize(c, (struct wlr_box){
				.x = stack_x,
				.y = m->w.y + ty,
				.width = stack_w,
				.height = h
			}, 0);
			
			ty += h - cell_height;
		}
		i++;
	}
}

void
dwindle(Monitor *m)
{
	Client *c;
	uint32_t tags = m->tagset[m->seltags];
	
	/* Create dwindle nodes for any tiled clients that don't have one */
	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen && !c->dwindle) {
			dwindle_create(c);
		}
	}
	
	/* Arrange using dwindle tree */
	dwindle_arrange(m, tags);
}

void
togglefloating(const Arg *arg)
{
	Client *sel = focustop(selmon);
	/* return if fullscreen */
	if (sel && !sel->isfullscreen)
		setfloating(sel, !sel->isfloating);
}

void
togglefullscreen(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		setfullscreen(sel, !sel->isfullscreen);
}

void
toggletag(const Arg *arg)
{
	uint32_t newtags;
	Client *sel = focustop(selmon);
	if (!sel || !(newtags = sel->tags ^ (arg->ui & TAGMASK)))
		return;

	sel->tags = newtags;
	focusclient(focustop(selmon), 1);
	arrange(selmon);
	printstatus();
}

void
toggleview(const Arg *arg)
{
	uint32_t newtagset;
	if (!(newtagset = selmon ? selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK) : 0))
		return;

	selmon->tagset[selmon->seltags] = newtagset;
	focusclient(focustop(selmon), 1);
	arrange(selmon);
	printstatus();
}

void
unlocksession(struct wl_listener *listener, void *data)
{
	SessionLock *lock = wl_container_of(listener, lock, unlock);
	destroylock(lock, 1);
}

void
unmaplayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *l = wl_container_of(listener, l, unmap);

	l->mapped = 0;
	wlr_scene_node_set_enabled(&l->scene->node, 0);
	if (l == exclusive_focus)
		exclusive_focus = NULL;
	if (l->layer_surface->output && (l->mon = l->layer_surface->output->data))
		arrangelayers(l->mon);
	if (l->layer_surface->surface == seat->keyboard_state.focused_surface)
		focusclient(focustop(selmon), 1);
	motionnotify(0, NULL, 0, 0, 0, 0);
}

void
unmapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is unmapped, and should no longer be shown. */
	Client *c = wl_container_of(listener, c, unmap);
	if (c == grabc) {
		cursor_mode = CurNormal;
		grabc = NULL;
	}

	if (client_is_unmanaged(c)) {
		if (c == exclusive_focus) {
			exclusive_focus = NULL;
			focusclient(focustop(selmon), 1);
		}
	} else {
		/* Remove from dwindle tree before removing from client list */
		dwindle_remove(c);
		wl_list_remove(&c->link);
		setmon(c, NULL, 0);
		wl_list_remove(&c->flink);
	}

	/* Clear frame buffers before destroying scene to prevent leaks */
	if (c->frame_top)
		wlr_scene_buffer_set_buffer(c->frame_top, NULL);
	if (c->frame_bottom)
		wlr_scene_buffer_set_buffer(c->frame_bottom, NULL);
	if (c->frame_left)
		wlr_scene_buffer_set_buffer(c->frame_left, NULL);
	if (c->frame_right)
		wlr_scene_buffer_set_buffer(c->frame_right, NULL);

	/* Free cached frame buffers */
	/* Drop our buffer references - scene already released via set_buffer(NULL) above */
	if (c->frame_top_buf) {
		wlr_buffer_drop(&c->frame_top_buf->base);
		c->frame_top_buf = NULL;
	}
	if (c->frame_bottom_buf) {
		wlr_buffer_drop(&c->frame_bottom_buf->base);
		c->frame_bottom_buf = NULL;
	}
	if (c->frame_left_buf) {
		wlr_buffer_drop(&c->frame_left_buf->base);
		c->frame_left_buf = NULL;
	}
	if (c->frame_right_buf) {
		wlr_buffer_drop(&c->frame_right_buf->base);
		c->frame_right_buf = NULL;
	}

	wlr_scene_node_destroy(&c->scene->node);
	printstatus();
	updatebars();
	motionnotify(0, NULL, 0, 0, 0, 0);
}

void
updatemons(struct wl_listener *listener, void *data)
{
	/*
	 * Called whenever the output layout changes: adding or removing a
	 * monitor, changing an output's mode or position, etc. This is where
	 * the change officially happens and we update geometry, window
	 * positions, focus, and the stored configuration in wlroots'
	 * output-manager implementation.
	 */
	struct wlr_output_configuration_v1 *config
			= wlr_output_configuration_v1_create();
	Client *c;
	struct wlr_output_configuration_head_v1 *config_head;
	Monitor *m;

	/* First remove from the layout the disabled monitors */
	wl_list_for_each(m, &mons, link) {
		if (m->wlr_output->enabled || m->asleep)
			continue;
		config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);
		config_head->state.enabled = 0;
		/* Remove this output from the layout to avoid cursor enter inside it */
		wlr_output_layout_remove(output_layout, m->wlr_output);
		closemon(m);
		m->m = m->w = (struct wlr_box){0};
	}
	/* Insert outputs that need to */
	wl_list_for_each(m, &mons, link) {
		if (m->wlr_output->enabled
				&& !wlr_output_layout_get(output_layout, m->wlr_output))
			wlr_output_layout_add_auto(output_layout, m->wlr_output);
	}

	/* Now that we update the output layout we can get its box */
	wlr_output_layout_get_box(output_layout, NULL, &sgeom);

	wlr_scene_node_set_position(&root_bg->node, sgeom.x, sgeom.y);
	wlr_scene_rect_set_size(root_bg, sgeom.width, sgeom.height);

	/* Make sure the clients are hidden when tbwm is locked */
	wlr_scene_node_set_position(&locked_bg->node, sgeom.x, sgeom.y);
	wlr_scene_rect_set_size(locked_bg, sgeom.width, sgeom.height);

	wl_list_for_each(m, &mons, link) {
		if (!m->wlr_output->enabled)
			continue;
		config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);

		/* Get the effective monitor geometry to use for surfaces */
		wlr_output_layout_get_box(output_layout, m->wlr_output, &m->m);
		m->w = m->m;
		wlr_scene_output_set_position(m->scene_output, m->m.x, m->m.y);

		wlr_scene_node_set_position(&m->fullscreen_bg->node, m->m.x, m->m.y);
		wlr_scene_rect_set_size(m->fullscreen_bg, m->m.width, m->m.height);

		if (m->lock_surface) {
			struct wlr_scene_tree *scene_tree = m->lock_surface->surface->data;
			wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
			wlr_session_lock_surface_v1_configure(m->lock_surface, m->m.width, m->m.height);
		}

		/* Calculate the effective monitor geometry to use for clients */
		arrangelayers(m);
		/* Reserve space for status bar at top (after arrangelayers) */
		m->w.y += cell_height;
		m->w.height -= cell_height;
		updatebar(m);
		/* Don't move clients to the left output when plugging monitors */
		arrange(m);
		/* make sure fullscreen clients have the right size */
		if ((c = focustop(m)) && c->isfullscreen)
			resize(c, m->m, 0);

		/* Try to re-set the gamma LUT when updating monitors,
		 * it's only really needed when enabling a disabled output, but meh. */
		m->gamma_lut_changed = 1;

		config_head->state.x = m->m.x;
		config_head->state.y = m->m.y;

		if (!selmon) {
			selmon = m;
		}
	}

	if (selmon && selmon->wlr_output->enabled) {
		wl_list_for_each(c, &clients, link) {
			if (!c->mon && client_surface(c)->mapped)
				setmon(c, selmon, c->tags);
			/* Restore clients to their previous monitor if it's back */
			else if (c->prev_mon_name[0]) {
				Monitor *restore_mon = NULL;
				Monitor *tm;
				wl_list_for_each(tm, &mons, link) {
					if (tm->wlr_output->enabled &&
					    strcmp(tm->wlr_output->name, c->prev_mon_name) == 0) {
						restore_mon = tm;
						break;
					}
				}
				if (restore_mon && c->mon != restore_mon) {
					setmon(c, restore_mon, c->tags);
					c->prev_mon_name[0] = '\0'; /* Clear after restore */
				}
			}
		}
		focusclient(focustop(selmon), 1);
		if (selmon->lock_surface) {
			client_notify_enter(selmon->lock_surface->surface,
					wlr_seat_get_keyboard(seat));
			client_activate_surface(selmon->lock_surface->surface, 1);
		}
	}

	/* FIXME: figure out why the cursor image is at 0,0 after turning all
	 * the monitors on.
	 * Move the cursor image where it used to be. It does not generate a
	 * wl_pointer.motion event for the clients, it's only the image what it's
	 * at the wrong position after all. */
	wlr_cursor_move(cursor, NULL, 0, 0);

	/* Update REPL display now that we have a monitor */
	updaterepl();

	wlr_output_manager_v1_set_configuration(output_mgr, config);
}

void
updatetitle(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_title);
	if (c == focustop(c->mon))
		printstatus();
	updateframe(c);
	updatebars();
}

void
urgent(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_activation_v1_request_activate_event *event = data;
	Client *c = NULL;
	toplevel_from_wlr_surface(event->surface, &c, NULL);
	if (!c || c == focustop(selmon))
		return;

	c->isurgent = 1;
	printstatus();

	if (client_surface(c)->mapped)
		client_set_border_color(c, urgentcolor);
}

void
view(const Arg *arg)
{
	if (!selmon || (arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
		return;
	selmon->seltags ^= 1; /* toggle sel tagset */
	if (arg->ui & TAGMASK)
		selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
	focusclient(focustop(selmon), 1);
	arrange(selmon);
	printstatus();
	updatebars();
}

void
virtualkeyboard(struct wl_listener *listener, void *data)
{
	struct wlr_virtual_keyboard_v1 *kb = data;
	/* virtual keyboards shouldn't share keyboard group */
	KeyboardGroup *group = createkeyboardgroup();
	/* Set the keymap to match the group keymap */
	wlr_keyboard_set_keymap(&kb->keyboard, group->wlr_group->keyboard.keymap);
	LISTEN(&kb->keyboard.base.events.destroy, &group->destroy, destroykeyboardgroup);

	/* Add the new keyboard to the group */
	wlr_keyboard_group_add_keyboard(group->wlr_group, &kb->keyboard);
}

void
virtualpointer(struct wl_listener *listener, void *data)
{
	struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
	struct wlr_input_device *device = &event->new_pointer->pointer.base;

	wlr_cursor_attach_input_device(cursor, device);
	if (event->suggested_output)
		wlr_cursor_map_input_to_output(cursor, device, event->suggested_output);
}

Monitor *
xytomon(double x, double y)
{
	struct wlr_output *o = wlr_output_layout_output_at(output_layout, x, y);
	return o ? o->data : NULL;
}

void
xytonode(double x, double y, struct wlr_surface **psurface,
		Client **pc, LayerSurface **pl, double *nx, double *ny)
{
	struct wlr_scene_node *node, *pnode;
	struct wlr_surface *surface = NULL;
	Client *c = NULL;
	LayerSurface *l = NULL;
	int layer;

	for (layer = NUM_LAYERS - 1; !surface && layer >= 0; layer--) {
		if (!(node = wlr_scene_node_at(&layers[layer]->node, x, y, nx, ny)))
			continue;

		if (node->type == WLR_SCENE_NODE_BUFFER) {
			struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(
					wlr_scene_buffer_from_node(node));
			if (scene_surface)
				surface = scene_surface->surface;
		}
		/* Walk the tree to find a node that knows the client */
		for (pnode = node; pnode && !c; pnode = &pnode->parent->node)
			c = pnode->data;
		if (c && c->type == LayerShell) {
			c = NULL;
			l = pnode->data;
		}
	}

	if (psurface) *psurface = surface;
	if (pc) *pc = c;
	if (pl) *pl = l;
}

void
zoom(const Arg *arg)
{
	Client *c, *sel = focustop(selmon);

	if (!sel || !selmon || !selmon->lt[selmon->sellt]->arrange || sel->isfloating)
		return;

	/* Search for the first tiled window that is not sel, marking sel as
	 * NULL if we pass it along the way */
	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, selmon) && !c->isfloating) {
			if (c != sel)
				break;
			sel = NULL;
		}
	}

	/* Return if no other tiled window was found */
	if (&c->link == &clients)
		return;

	/* If we passed sel, move c to the front; otherwise, move sel to the
	 * front */
	if (!sel)
		sel = c;
	wl_list_remove(&sel->link);
	wl_list_insert(&clients, &sel->link);

	focusclient(sel, 1);
	arrange(selmon);
}

#ifdef XWAYLAND
void
activatex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, activate);

	/* Only "managed" windows can be activated */
	if (!client_is_unmanaged(c))
		wlr_xwayland_surface_activate(c->surface.xwayland, 1);
}

void
associatex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, associate);

	LISTEN(&client_surface(c)->events.map, &c->map, mapnotify);
	LISTEN(&client_surface(c)->events.unmap, &c->unmap, unmapnotify);
}

void
configurex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, configure);
	struct wlr_xwayland_surface_configure_event *event = data;
	if (!client_surface(c) || !client_surface(c)->mapped) {
		wlr_xwayland_surface_configure(c->surface.xwayland,
				event->x, event->y, event->width, event->height);
		return;
	}
	if (client_is_unmanaged(c)) {
		wlr_scene_node_set_position(&c->scene->node, event->x, event->y);
		wlr_xwayland_surface_configure(c->surface.xwayland,
				event->x, event->y, event->width, event->height);
		return;
	}
	if ((c->isfloating && c != grabc) || !c->mon->lt[c->mon->sellt]->arrange) {
		resize(c, (struct wlr_box){.x = event->x - c->bw,
				.y = event->y - c->bw, .width = event->width + c->bw * 2,
				.height = event->height + c->bw * 2}, 0);
	} else {
		arrange(c->mon);
	}
}

void
createnotifyx11(struct wl_listener *listener, void *data)
{
	struct wlr_xwayland_surface *xsurface = data;
	Client *c;

	/* Allocate a Client for this surface */
	c = xsurface->data = ecalloc(1, sizeof(*c));
	c->surface.xwayland = xsurface;
	c->type = X11;
	c->bw = client_is_unmanaged(c) ? 0 : borderpx;

	/* Listen to the various events it can emit */
	LISTEN(&xsurface->events.associate, &c->associate, associatex11);
	LISTEN(&xsurface->events.destroy, &c->destroy, destroynotify);
	LISTEN(&xsurface->events.dissociate, &c->dissociate, dissociatex11);
	LISTEN(&xsurface->events.request_activate, &c->activate, activatex11);
	LISTEN(&xsurface->events.request_configure, &c->configure, configurex11);
	LISTEN(&xsurface->events.request_fullscreen, &c->fullscreen, fullscreennotify);
	LISTEN(&xsurface->events.set_hints, &c->set_hints, sethints);
	LISTEN(&xsurface->events.set_title, &c->set_title, updatetitle);
}

void
dissociatex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, dissociate);
	wl_list_remove(&c->map.link);
	wl_list_remove(&c->unmap.link);
}

void
sethints(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_hints);
	struct wlr_surface *surface = client_surface(c);
	if (c == focustop(selmon) || !c->surface.xwayland->hints)
		return;

	c->isurgent = xcb_icccm_wm_hints_get_urgency(c->surface.xwayland->hints);
	printstatus();

	if (c->isurgent && surface && surface->mapped)
		client_set_border_color(c, urgentcolor);
}

void
xwaylandready(struct wl_listener *listener, void *data)
{
	struct wlr_xcursor *xcursor;

	/* assign the one and only seat */
	wlr_xwayland_set_seat(xwayland, seat);

	/* Set the default XWayland cursor to match the rest of tbwm. */
	if ((xcursor = wlr_xcursor_manager_get_xcursor(cursor_mgr, "default", 1)))
		wlr_xwayland_set_cursor(xwayland,
				xcursor->images[0]->buffer, xcursor->images[0]->width * 4,
				xcursor->images[0]->width, xcursor->images[0]->height,
				xcursor->images[0]->hotspot_x, xcursor->images[0]->hotspot_y);
}
#endif

int
main(int argc, char *argv[])
{
	char *startup_cmd = NULL;
	int c;

	while ((c = getopt(argc, argv, "s:hdv")) != -1) {
		if (c == 's')
			startup_cmd = optarg;
		else if (c == 'd')
			log_level = WLR_DEBUG;
		else if (c == 'v')
			die("tbwm " VERSION);
		else
			goto usage;
	}
	if (optind < argc)
		goto usage;

	/* Wayland requires XDG_RUNTIME_DIR for creating its communications socket */
	if (!getenv("XDG_RUNTIME_DIR"))
		die("XDG_RUNTIME_DIR must be set");
	setup();
	run(startup_cmd);
	cleanup();
	return EXIT_SUCCESS;

usage:
	die("Usage: %s [-v] [-d] [-s startup command]", argv[0]);
}
