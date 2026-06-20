# game_pang — PANG / Buster Bros (GEN2 HGR, C)

A bubble-buster built on the `demo_bounces` rendering techniques: double
buffering, incremental **XOR** erase, fast **7px/byte** blit, and **pre-tinted**
artifact-colour sprites. Bubbles fall in **gravity arcs** (bouncing off the
floor and a few mid-air platforms) and split big → medium → small → pop.

Controls are **TMS-Galaga style**: the cannon *glides* (a key latches a
direction, applied every frame), and **SPACE** fires fast **bullet pellets** (a
small pool, short cooldown) — not an instant ray. **S** stops the cannon *and*
fires an aimed shot. Bubble/bullet hits use **circle collision** (centre
distance vs radii). Shooting a **platform erodes** the segment you hit — the
most-shot spots wear into holes that bullets and bubbles fall through.

Pick the keyboard at the title screen:

- **U** → US / QWERTY (`A` = left)
- **F** → FR / AZERTY (`Q` = left)

```bash
make -C sketchs/gen2/game_pang
build/POM1 --preset 11 \
    --load 6000:"software/Graphic HGR/GEN2Pang.bin" --run 6000
```

Keys: **A**/**Q** glide left, **D** glide right, **S** stop+fire, **SPACE** fire.
