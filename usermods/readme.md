# Usermods

This directory now tracks the bundled usermods that still make sense for the native macOS/Linux port.

Hardware-only usermods that depended on ESP GPIO, sensors, displays, relays, RF/IR peripherals, SD hardware, or ESP-specific networking/power features have been removed from the native product path. The remaining entries are either:

- host-port targets that can still be adapted to the native runtime, or
- source examples for writing new host-compatible usermods.

If you add a new bundled usermod, keep these rules in mind:

- Create a descriptive folder under `usermods/`.
- Keep it host-compatible, or gate hardware-only code to the Arduino path and document the host boundary explicitly.
- If it needs changes elsewhere in WLED, include a `readme.md` that explains the integration steps.
- Add tests for config, JSON hooks, and runtime behavior when the usermod is enabled in the native host build.

For new usermods, prefer the v2 API style and use `usermods/EXAMPLE/` plus `usermods/usermod_v2_auto_save/` as the current host-oriented references.
