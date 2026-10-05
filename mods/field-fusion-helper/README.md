# Field Fusion Helper

Field Fusion Helper extends fusion assistance to monsters that are already on the player's field.

For each occupied monster zone, it searches the current hand for the best valid route starting from that field monster, including multi-card chains and equips. It uses the game's active fusion, equip, terrain and stat-limit rules, so data mods are reflected automatically.

`F1` to `F5` are the player's monster zones from left to right. `H1` to `H5` are the hand slots from left to right. For example:

```
F1  H4>H1  >  Flame Cerebrus  2100/1800
```

means: start with the monster in field zone 1, then play hand slot 4, then hand slot 1.

The built-in **View > Fusion Helper** does not need to be enabled; this mod is independent.

## Settings

- **Text size**: defaults to 3x; Auto follows the port UI scale.
- **Position**: top left, centre or right.
- **Style**: menu panel or minimal text.
- **Show when no field fusion exists**: off by default.

## Installation

When installed separately, place the `field-fusion-helper` directory in either:

- `Documents\\My Games\\YFM Re-Decomp\\mods\\` on Windows, or
- the game's `mods/` directory.

Restart the game, then enable **Field Fusion Helper** under **Game > Mods**.
