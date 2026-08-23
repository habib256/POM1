// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//
// CardTypes.h — board-configuration enums that appear in Memory's public API.
//
// Why they are not nested in their cards: Memory holds every peripheral by
// unique_ptr and forward-declares it (see the note at the top of Memory.h), so
// none of the card headers needs to be in that file — except that a NESTED type
// cannot be forward-declared. Three of them appear in Memory's signatures
// (JukeBox::Jumper, JukeBox::ChipMode, CodeTank::Jumper), and those three alone
// kept JukeBox.h and CodeTank.h in the most widely-included header of the
// project: `touch src/JukeBox.h` recompiled 105 translation units although only
// 15 name JukeBox at all (measured 23 août 2026), and the eleven other cards had
// already been forward-declared away.
//
// Hoisting them to namespace scope in a header with no dependencies of its own
// (not even <cstdint> beyond the fixed underlying type) costs one include in
// Memory.h and frees the two card headers.
//
// The cards keep member aliases — `using Jumper = pom1::JukeBoxJumper;` — so
// every one of the 164 existing `JukeBox::Jumper` / `CodeTank::Jumper` /
// `JukeBox::ChipMode` spellings across src/ and tests/ still names the same
// type and did not have to change. New code may use either; the qualified
// card-scoped form reads better at a call site, the namespace-scope form is
// what a signature in Memory.h must use.

#ifndef POM1_CARD_TYPES_H
#define POM1_CARD_TYPES_H

#include <cstdint>

namespace pom1 {

/// Juke-Box RAM/ROM split, set by a board jumper (see JukeBox.h).
enum class JukeBoxJumper : uint8_t {
    RAM16_ROM32 = 0,  // $4000-$BFFF (32 kB), RAM to $3FFF
    RAM32_ROM16 = 1,  // $8000-$BFFF (16 kB), RAM to $7FFF
};

/// Physical chip socketed on the Juke-Box card. Per Parmigiani/Rosselli you
/// physically swap between one and the other; POM1 exposes it as a setting.
enum class JukeBoxChipMode : uint8_t {
    Flash        = 0, // Paged read-only, 16 kB to 512 kB (default).
    EEPROM28C256 = 1, // Single-page 32 kB 28c256, writable.
};

/// CodeTank bank jumper — which 16 kB half of the 32 kB 28c256 is visible
/// at $4000-$7FFF (see CodeTank.h).
enum class CodeTankJumper : uint8_t {
    Lower16 = 0,  // file offset $0000-$3FFF visible at $4000-$7FFF
    Upper16 = 1,  // file offset $4000-$7FFF visible at $4000-$7FFF
};

} // namespace pom1

#endif // POM1_CARD_TYPES_H
