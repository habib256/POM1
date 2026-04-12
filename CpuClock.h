#ifndef POM1_CPU_CLOCK_H
#define POM1_CPU_CLOCK_H

// Horloge 6502 : 1.022727 MHz (quartz NTSC 14.31818 MHz / 14).
inline constexpr int POM1_CPU_CLOCK_HZ = 1022727;

// Budget ~60 Hz : arrondi au cycle le plus proche (voir EmulationController / UI).
inline constexpr int POM1_CPU_CYCLES_PER_FRAME_1X_60HZ = (POM1_CPU_CLOCK_HZ + 30) / 60;
inline constexpr int POM1_CPU_CYCLES_PER_FRAME_2X_60HZ = (2 * POM1_CPU_CLOCK_HZ + 30) / 60;

// ~1 ms en cycles machine (sondes modem / terminal).
inline constexpr int POM1_CPU_CYCLES_PER_MILLISECOND = (POM1_CPU_CLOCK_HZ + 500) / 1000;

#endif // POM1_CPU_CLOCK_H
