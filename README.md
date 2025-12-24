<img width="1300" height="300" alt="logo" src="https://github.com/user-attachments/assets/18a14fa4-ebef-4780-a661-e86016124134" />


# TurboWM (tbwm)

a dynamic tiling wayland compositor with shitty scheme scripting

features:
- scheme configuration: lots configurable at runtime via ~/.config/tbwm/config.scm (not that good tho)
- launcher: you can press super+d for application launcher. i should probably make it a seperate app but like uhuhuj
- scheme repl: (super+shift+;)

building:
dependencies:
- wlroots 0.19+
- wayland, wayland-protocols
- libinput, xkbcommon
- freetype2, pangocairo
- libxcb, xcb-icccm (for XWayland)

(you should also get foot)

```pacman
sudo pacman -S wlroots0.19 wayland-protocols wayland libinput libxkbcommon freetype2 pango libxcb xcb-util-wm foot
```
```apt
sudo apt install libwlroots-dev wayland-protocols libwayland-dev libinput-dev libxkbcommon-dev libfreetype6-dev libpango1.0-dev libxcb1-dev libxcb-icccm4-dev
```

then run install.sh
then ./tbwm

configuration is done via ~/.config/tbwm/config.scm
