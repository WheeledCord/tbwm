# dwl (i need a new name)

## Building

Dependencies:
- wlroots 0.19+
- wayland, wayland-protocols
- libinput, xkbcommon
- freetype2, pangocairo
- libxcb, xcb-icccm (for XWayland)

```bash
make
```

## Running

you dont need it but you should install foot bc it uses foot tho you can change it, it adds its own config ykyk

```bash
./dwl
```

Or from a TTY:
```bash
./dwl -s 'foot'
```

## Configuration

Configuration is done via `~/.config/dwl/config.scm`. A default config is created automatically on first run.

### Keybinding Syntax

```scheme
(bind-key "M-Return" (lambda () (spawn "foot")))
(bind-key "M-S-q" (lambda () (quit)))
```

Modifiers: `M` = Super, `S` = Shift, `C` = Control, `A` = Alt

### Available Functions

**Window Management:**
- `(spawn cmd)` - launch a program
- `(quit)` - exit dwl
- `(kill-client)` - close focused window
- `(toggle-floating)` / `(toggle-fullscreen)`
- `(zoom)` - swap with master
- `(refresh)` - refresh layout

**Navigation:**
- `(focus-dir DIR-LEFT/RIGHT/UP/DOWN)` - focus window in direction
- `(swap-dir dir)` - swap window in direction  
- `(focus-stack 1)` / `(focus-stack -1)` - focus next/prev
- `(focus-monitor MON-LEFT/RIGHT)` - focus monitor

**Tags:**
- `(view-tag n)` - switch to tag 1-9
- `(tag-window n)` - move window to tag
- `(toggle-tag n)` - toggle tag visibility
- `(view-all)` - view all tags
- `(tag-all)` - sticky window

**Layouts:**
- `(set-layout "tile"/"dwindle"/"monocle"/"float")`
- `(cycle-layout)` - cycle through layouts
- `(inc-mfact 0.05)` - adjust master size

**Meta:**
- `(reload-config)` - reload config without restart
- `(log msg)` - print to stderr

### Default Keybindings

| Key | Action |
|-----|--------|
| `Super+Return` | Terminal |
| `Super+d` | Launcher |
| `Super+q` | Close window |
| `Super+Shift+e` | Quit |
| `Super+h/j/k/l` | Focus direction |
| `Super+Shift+h/j/k/l` | Swap windows |
| `Super+f` | Fullscreen |
| `Super+Shift+Space` | Toggle floating |
| `Super+1-9` | Switch tag |
| `Super+Shift+1-9` | Move to tag |
| `Super+Shift+c` | Reload config |

## License

See LICENSE files.
