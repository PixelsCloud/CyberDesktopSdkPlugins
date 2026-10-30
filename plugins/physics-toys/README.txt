CyberDesktop Physics Toys 1.2.0

Wind chime: cartoon moon, bell and star with articulated tails. Drag a charm;
double-click to pluck. Gentle wind is optional. This version has no audio.
Spring clock: local HH:MM:SS, 12/24 hour format, spring-mounted cards. Changed digits
bounce every second in both 2x4 and compact 1x4 layouts. Drag and release a card, or double-click to bounce.

Both follow light/dark themes, offer pastel/ocean palettes, transparent backgrounds,
speed (default 0.75), pause and reduced motion. Hidden widgets stop their timer;
idle clocks only check wall time once per second. Physics stays inside each widget.

Uses CyberCanvas, Box2D 3.1.1 and FastNoiseLite 1.1.1; MIT notices are in licenses/.

Independent mechanical editions: spring clock 4x1 / 4x2 and wind chime 2x2 only.
They have their own IDs, packages and fixed steel/brass PNG components. The six
clock plaques and all chime pendants follow their actual Box2D attachment points.
Noise-driven gusts change direction and intensity continuously, with a different
seed per live instance. Wind is applied to main pendants and tails once per fixed
solver step; it fades near tile edges. The default mechanical breeze is 0.55.
Hidden/reduced-motion instances stop wind and timers; resume does not catch up
offline time. Traditional chimes also receive the improved visible random breeze.
Artwork provenance: assets/mechanical/<variant>/provenance.json.
Focused checks: scripts/build-mechanical-air-toys.ps1 -Jobs 10.
