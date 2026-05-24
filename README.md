# Browser Freezer

![](doc/social-preview.jpg)

**browser-freezer** is a small and light utility to freeze (stop) browsers when screen blanks on linux+X11.

I have a small NUC-style mini PC that I run on linux with X11 (Xubuntu), but I cannot suspend it, because off some hardware incompatibilities. So, in order to minimize power consumption and heat generation, I made this small utility to freeze the browser when the screen blanks, and un-freeze it on wakeup.

It is composed of two parts:
- a tiny C daemon **browser-freezer** that polls the state of the monitor (On / Off) with as less CPU usage as possible (no forks)
- on monitor status change detection, run the bash script **browser-freezer-signal** that sends the SIGSTOP or SIGCONT signal to all browser processes (firefox by default). We use a bash script for the most possible flexibility and customization, as performance is not an issue there.

## Installation

```
make install
```

Make `browser-freezer` a startup application for your desktop

`make restart` compiles, installs, and runs the process. Must be run under your account, not root.

## Configuration

The list of browsers can be set in your `.config/browser-freezer.conf` file, as a bash array "browsers" of bash globbing patterns on the executable path of your browser.

E.g:

```
browsers=(
    '/usr/lib/firefox/firefox*'
    '*/usr/lib/chromium-browser/chrome'
    opt/google/chrome/chrome
)
```
