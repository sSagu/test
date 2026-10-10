# Approved UI design (Sueño-Guía v2)

Approved by the owner on 2026-10-10. Live canvas: https://claude.ai/artifact/MbYBfaR45aYApNpBgymvPG (private).

- `main-screen.dc.html`: main screen. Tweak `estado` = antes | disponible | anotado | sinalarma shows the four
  states of the "Me voy a dormir" control (see `renderVals()`); `acento` = #8AB4F8.
- `settings-screen.dc.html`: settings moved to their own screen (gear icon top-right on main; back arrow).

Design tokens (dp / sp, frame 412×892 dp = S25 FE):
- Ground #000000 (true black, AMOLED). Cards #141416, radius 24, padding 20. Dividers #26262B.
- Text primary #EDEDED / #F4F4F6, secondary #C8C8D0, muted #A8A8B0, footnote #8A8A92.
- Accent #8AB4F8 (buttons, switches on, check badge) with on-accent text #0B1A33.
- Chart bars #5B8DE6 (validated for dark surface: lightness band + 3:1 contrast), width 20, top radius 4,
  2 dp gap between columns, baseline #2E2E34, dashed 8 h target line #6B6B73 labelled "objetivo 8 h",
  scale 0–10 h over 120 dp. Unlogged nights: short #4A4A52 dash at the baseline, never a 0-height bar.
  Only the latest logged night carries a value label. Every column has a content description
  ("vie 9/10: 7 h 55 min de sueño" / "… : sin registro").
- Primary button: height 64, fully rounded, accent fill, 18 sp semibold, moon icon. Disabled: #101012 fill,
  1 dp #2A2A30 stroke, #8A8A92 text. Secondary/outline: 48 high, 1 dp #3A3A42 stroke.
- Destructive text button: #F2A39C on transparent with #3A2A2A stroke.
- Big alarm time: 64 sp light, tabular figures. Section titles 16 sp semibold. Body 15–16 sp.
- Touch targets ≥ 48 dp (steppers 44 dp visual inside a 48 dp row is acceptable only if the hit area is ≥ 48 dp).
- Icons: outline stroke icons (gear, back, bell, moon, check) as vector drawables.

## v3 addition: manual night entry (approved with "que se demore todo lo que se necesite")

- `main-screen.dc.html` now ends the week card with a text link "+ Anotar una noche a mano" (accent, ≥48 dp).
- `manual-night-sheet.dc.html`: bottom sheet (#1A1A1D, top radius 28, grab handle) over the dimmed main screen:
  title "Anotar una noche a mano", subtitle "Para las noches que no anotaste con el botón.",
  night picker ‹ "hoy, sáb 10/10" / "noche del vie al sáb" › (last 7 nights, next disabled on today),
  "Dormiste" stepper − "6 h 00 min" + (15-min steps), note "Cuenta como sueño en el gráfico y en la deuda
  de la semana. Si esa noche ya estaba anotada, la reemplaza.", buttons Cancelar (outline) / Guardar (primary).
