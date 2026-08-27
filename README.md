# FPP Art-Net Prop Control (Secondary) — FPP 10

A separate FPP 10 plugin derived from the primary Art-Net Prop Control design. It keeps the Letters controls unchanged, but splits the 2,000-pixel Festoon into two independently controlled alternating pixel groups.

## Festoon split

- **Festoon A:** pixel numbers **1, 3, 5, 7...1999** — 1,000 pixels
- **Festoon B:** pixel numbers **2, 4, 6, 8...2000** — 1,000 pixels

Both groups still occupy the same physical FPP Festoon channel range **1-6000**. The plugin simply processes alternating RGB pixels using different Art-Net controls.

## Art-Net slot map

| Slot | Function |
|---:|---|
| 1 | Global Master — all props/groups |
| 2 | Letters brightness |
| 3 | Letters red |
| 4 | Letters green |
| 5 | Letters blue |
| 6 | Letters colour mode |
| 7-9 | Spare |
| 10 | Festoon A brightness — pixels 1,3,5... |
| 11 | Festoon A red |
| 12 | Festoon A green |
| 13 | Festoon A blue |
| 14 | Festoon A colour mode |
| 15-19 | Spare |
| 20 | Festoon B brightness — pixels 2,4,6... |
| 21 | Festoon B red |
| 22 | Festoon B green |
| 23 | Festoon B blue |
| 24 | Festoon B colour mode |

## Colour modes

Channels **6, 14 and 24** are independent colour-mode selectors:

- **0-127 — Full source colour:** preserve the active xLights/FPP Sequence or FPP Effect RGB colour for that group. RGB sliders for that group are ignored while the source is active.
- **128-255 — Desk colour override:** preserve only the source per-pixel intensity/pattern and recolour it using that group's Art-Net RGB values.

When no Sequence/Effect is active, each group shows its selected solid desk colour on its assigned pixels.

## Processing order

For every assigned pixel:

1. Select source colour or desk-colour override.
2. Apply that group's local brightness.
3. Apply **Channel 1 Master last**.

This means Master always dims Letters, Festoon A and Festoon B together regardless of mode.

## Confirmed FPP pixel layout

- **Festoon:** channels **1-6000**, 2,000 RGB pixels
- **Letters:** channels **6001-6447**, 149 RGB pixels
- **Art-Net control block:** default **10001-10024**

Configure FPP Channel Input to map at least 24 Art-Net slots starting at FPP channel 10001.

## Required FPP bridge setting

Set:

**Settings → Input/Output → Bridge Data Priority → Prioritize Bridge**

The Art-Net control block is outside the sequence pixel range, so it does not overlap channels 1-6447.

## Effect / overlay support

As in the current primary plugin, the Secondary plugin snapshots the prop data before FPP Effects/Pixel Overlays and compares it with the final frame. It detects Effect activity independently for Letters, Festoon A and Festoon B, with a 750 ms hold to preserve deliberate black frames inside effects.

## Install / update

Recommended separate repository:

`lindsayrobinson/fpp-artnet-prop-control-secondary`

Then on FPP 10:

```bash
cd /home/fpp/media/plugins
git clone https://github.com/lindsayrobinson/fpp-artnet-prop-control-secondary.git
cd fpp-artnet-prop-control-secondary
chmod +x callbacks.sh scripts/*.sh
./scripts/fpp_install.sh
sudo systemctl restart fppd
```

The plugin installs with **Bypass ON**. Confirm the control mapping before turning Bypass off.

You may keep the original/Primary plugin installed, but do **not** run both plugins with Bypass off at the same time because both modify FPP channels 1-6447. Use one active plugin at a time.

## Version

Custom **v1.0 / Secondary / FPP 10** build.
