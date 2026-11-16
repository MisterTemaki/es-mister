#pragma once
#ifndef ES_CORE_RENDERERS_SOFT_MISTER_PADS_H
#define ES_CORE_RENDERERS_SOFT_MISTER_PADS_H

// The MiSTer's menu button on a controller (the DualSense's PS button, as Main_MiSTer's map for it says:
// /media/fat/config/inputs/input_VVVV_PPPP_v3.map, the two codes held together that open its menu) asks
// first while ES is on the screen. Main_MiSTer reads every controller too, and opened its menu over ES at
// once: "it leaves ES". While ES's console is shown, the controllers ES reads that have such a button are
// ES's alone (EVIOCGRAB on SDL's own descriptor, so SDL still reads them); ES asks, and on yes gives them
// back and presses F12 for Main_MiSTer (es-mister-helper osd). When ES's console is let go of (the MiSTer
// menu over it, a game), they are given back at once, by the console's thread, whatever ES's is doing.
// Keyboards are never taken (F12 always opens the MiSTer menu), nor virtual devices (Zaparoo's).
//
//   ES_MISTER_MENU_ASK=1  on (es-mister.sh, MENU_ASK in es-mister.ini)
//   ES_MISTER_HELPER      es-mister-helper's path

#include <string>

namespace Renderer
{
	namespace Soft
	{
		namespace MisterPads
		{
			// the console's thread
			void grab();             // the controllers with a MiSTer menu button ES's (new ones too)
			void release();          // given back to Main_MiSTer
			void shown(bool shown);  // ES's console on the screen, or let go of

			// ES's thread
			bool hidden();           // ES's console not on the screen: what is pressed is not for ES
			// a button of a controller (SDL's device path and index) pressed or let go: true when it is (part
			// of) the MiSTer menu's, which ES keeps; ask true when the press asks for the MiSTer menu
			bool menuButton(const std::string& devicePath, int button, bool pressed, bool& ask);
			// yes: the controllers given back and the MiSTer menu opened
			void openMisterMenu();
		}
	}
}

#endif
