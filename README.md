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
(tho you should also get foot)

then run make

run with ./tbwm

configuration is done via ~/.config/tbwm/config.scm
