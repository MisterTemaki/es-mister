#pragma once
#ifndef ES_CORE_RENDERERS_SOFT_MISTER_PADS_H
#define ES_CORE_RENDERERS_SOFT_MISTER_PADS_H

// The MiSTer's menu button on a controller (the DualSense's PS button, as Main_MiSTer's map for it says:
// /media/fat/config/inputs/input_VVVV_PPPP_v3.map, the two codes held together that open its menu) asks
// first while ES is on the screen. Main_MiSTer reads every controller too, and opened its menu over ES at
// once: "it leaves ES". While ES is shown, the controllers ES reads that have such a button are ES's alone
// (EVIOCGRAB on SDL's own descriptor, so SDL still reads them); ES asks, and on yes gives them back and
// closes (es-mister.sh loads the MiSTer menu).
//
// ES cannot see the MiSTer menu over it (no console switch, nothing to read), so it holds the controllers
// only while nothing else may have the screen: its console shown (given back by the console's thread when
// let go of, whatever ES's thread is doing), the menu core loaded (/tmp/CORENAME: not a core Zaparoo or a
// script loaded), and no F12 pressed on a keyboard since (the MiSTer menu over ES, which the controllers
// must reach; held again once ES's console has been let go of and is back, or at ES's next start).
// Keyboards are never taken (F12 always opens the MiSTer menu), nor virtual devices (Zaparoo's).
//
//   ES_MISTER_MENU_ASK=1  on (es-mister.sh, MENU_ASK in es-mister.ini)

#include <map>
#include <string>
#include <vector>

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
			// The buttons of a controller (its device path) as SDL numbers them on this kernel, against the
			// numbers a mapping made for the standard gamepad buttons alone gives them (SDL's database,
			// Batocera's es_input.cfg): the MiSTer's hid-playstation also gives the DualSense BTN_Z, after
			// its face buttons, and every button from L1 on came one number later (Create read as START,
			// Options as nothing). Old number -> new, with START's; empty when they agree, or for a
			// controller with other keys than a gamepad's (BTN_C and BTN_Z apart), left as it is.
			std::map<int, int> standardRenumbering(const std::string& devicePath, int& oldStart, int& newStart);
			// the same, from the controller's key codes in SDL's order
			std::map<int, int> renumberingOf(const std::vector<int>& codes, int& oldStart, int& newStart);

			// the controllers given back, not taken again until ES's console is back: yes to the question
			// (ES closes), F12 on a keyboard (the MiSTer menu over ES)
			void giveBack(const char* why);
		}
	}
}

#endif
