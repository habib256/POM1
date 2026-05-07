#ifndef POM1_CPU_CLOCK_H
#define POM1_CPU_CLOCK_H

// Horloge 6502 : 1.022727 MHz (quartz NTSC 14.31818 MHz / 14).
inline constexpr int POM1_CPU_CLOCK_HZ = 1022727;

// Frame rate NTSC réel = 59.94 Hz (= 60 × 1000/1001), pas 60 Hz rond.
// 1022727 / 59.94005994 ≈ 17062.49 → 17062 cycles/frame (rounded to nearest).
// Was 17045 cycles/frame (60 Hz round); le drift de ~0.1% s'aligne avec le
// silicon réel pour les démos audio fines et les multiplexages SAT timing
// -critiques (cf. dev/SILICONBUGS.md Bug N°11). Le nom de la constante
// reste 60HZ pour compat avec les call-sites historiques.
inline constexpr int POM1_CPU_CYCLES_PER_FRAME_1X_60HZ = (1001 * POM1_CPU_CLOCK_HZ + 30000) / 60000;
inline constexpr int POM1_CPU_CYCLES_PER_FRAME_2X_60HZ = (1001 * 2 * POM1_CPU_CLOCK_HZ + 30000) / 60000;

// ~1 ms en cycles machine (sondes modem / terminal).
inline constexpr int POM1_CPU_CYCLES_PER_MILLISECOND = (POM1_CPU_CLOCK_HZ + 500) / 1000;

#endif // POM1_CPU_CLOCK_H
