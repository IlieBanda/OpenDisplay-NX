# Switch interface

The screen draws with SDL on the existing main thread and uses libnx pl:u
shared Nintendo UDSG font. Simplified Chinese glyphs prefer the console
Chinese shared font. No Nintendo font file is distributed with this port.
UTF-8 glyphs are packed once into an atlas; all four translation catalogs
are checked for completeness at compile time. The saved language integer
is validated before use.

Layout references:
- https://github.com/natinusala/borealis/blob/main/library/lib/core/style.cpp
- https://github.com/natinusala/borealis/blob/main/library/lib/core/theme.cpp
- https://en-americas-support.nintendo.com/app/answers/detail/a_id/22357/~/how-to-change-the-theme

The frame uses 88 px header and 73 px footer, a 380 px information pane,
70 px list rows with 24 px labels and 20 px right-aligned values. It is
a custom SDL screen inspired by these patterns, not a system applet or
a Borealis integration. Focus has a five-pixel animated outline; button
hints use NintendoExt shared-font button symbols (Plus U+E0EF, B U+E0E1, A U+E0E0). The console theme selects Basic White/Black.

Plus toggles the menu; B returns to video. A confirms the selected row:
decoder, statistics, language, disconnect, exit. Both sticks and D-pad
navigate; touch uses the same list geometry as drawing. X/Y/Minus have
no playback shortcuts. Language is saved in language.txt on SD.

Host preview checks cover every translated glyph with real font atlases
and render all four languages. The optional local preview can use a
Nintendo UDSG font from an existing reference checkout; those font files
are excluded from release archives. On-device menu/input/font validation
of all four languages is still recommended for the first public release.
