# Changelog

## 1.0.0

First public release, for Diablo II: Resurrected 3.3 under D2RLoader 1.3.1.

- While the waypoint panel is open, the mouse wheel switches Act tabs: down for the next Act, up for the previous,
  wrapping at both ends. The cursor can be anywhere.
- It uses the tab bar's own previous/next, the same as a controller's shoulder buttons. Acts not reached yet are
  skipped, and Classic characters cycle Acts I–IV.
- It never selects a destination. With the panel closed, the wheel is untouched.
- Built on D2R 3.3.0 build 93847 with D2RLoader 1.3.1. It finds the game code by signature rather than fixed
  addresses, so it also loads on other versions. Where that code has changed, scrolling does nothing.
