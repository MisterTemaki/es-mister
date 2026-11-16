// On the MiSTer a controller's Select plays ES's "start" (the menu) and its Start plays "select"
// (InputConfig::isMappedTo); keyboards keep theirs; the configured names stay (isMappedAs, getInputByName).
#include "TestMain.h"
#include "InputConfig.h"

TEST(controller_select_opens_the_menu)
{
	InputConfig pad(0, 0, "pad", "guid", 13, 1, 6);
	pad.mapInput("select", Input(0, TYPE_BUTTON, 8, 1, true));
	pad.mapInput("start", Input(0, TYPE_BUTTON, 9, 1, true));
	pad.mapInput("hotkey", Input(0, TYPE_BUTTON, 8, 1, true));
	const Input selectButton(0, TYPE_BUTTON, 8, 1, false), startButton(0, TYPE_BUTTON, 9, 1, false);
	CHECK(pad.isMappedTo("start", selectButton));
	CHECK(!pad.isMappedTo("start", startButton));
	CHECK(pad.isMappedTo("select", startButton));
	CHECK(!pad.isMappedTo("select", selectButton));
	CHECK(pad.isMappedTo("hotkey", selectButton));
	CHECK(pad.isMappedAs("select", selectButton));
	CHECK_EQ(InputConfig::buttonLabel("start"), std::string("select"));

	InputConfig keyboard(DEVICE_KEYBOARD, -1, "Keyboard", "-1", 0, 0, 0);
	keyboard.mapInput("start", Input(DEVICE_KEYBOARD, TYPE_KEY, 32, 1, true));
	keyboard.mapInput("select", Input(DEVICE_KEYBOARD, TYPE_KEY, 8, 1, true));
	CHECK(keyboard.isMappedTo("start", Input(DEVICE_KEYBOARD, TYPE_KEY, 32, 1, false)));
}

#if defined(__linux__)
#include "renderers/soft/MisterPads.h"
#include <linux/input.h>

// The DualSense on the MiSTer's kernel (hid-playstation with BTN_Z): a mapping made without BTN_Z
// (SDL's database) numbered anew; a gamepad without extra keys left as it is; one with other keys too
TEST(dualsense_buttons_numbered_as_the_kernel_does)
{
	using Renderer::Soft::MisterPads::renumberingOf;
	int oldStart = 0, newStart = 0;
	const std::vector<int> dualsense = { BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_Z, BTN_TL, BTN_TR, BTN_TL2, BTN_TR2,
		BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR };
	std::map<int, int> renumbering = renumberingOf(dualsense, oldStart, newStart);
	CHECK_EQ(oldStart, 9);
	CHECK_EQ(newStart, 10);
	CHECK_EQ(renumbering.size(), (size_t)9);
	CHECK_EQ(renumbering[4], 5);  // L1
	CHECK_EQ(renumbering[8], 9);  // Create: select
	CHECK_EQ(renumbering[11], 12); // L3
	CHECK(renumbering.find(3) == renumbering.end());

	InputConfig pad(0, 0, "DualSense", "guid", 14, 1, 6);
	pad.mapInput("select", Input(0, TYPE_BUTTON, 8, 1, true));
	pad.mapInput("start", Input(0, TYPE_BUTTON, 9, 1, true));
	pad.mapInput("up", Input(0, TYPE_HAT, 0, 1, true));
	pad.renumberButtons(renumbering);
	Input input;
	CHECK(pad.getInputByName("select", &input) && input.id == 9);
	CHECK(pad.getInputByName("start", &input) && input.id == 10);
	CHECK(pad.getInputByName("up", &input) && input.id == 0 && input.type == TYPE_HAT);

	const std::vector<int> plain = { BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR, BTN_SELECT, BTN_START, BTN_MODE };
	CHECK(renumberingOf(plain, oldStart, newStart).empty());
	CHECK_EQ(oldStart, newStart);

	const std::vector<int> joystick = { BTN_TRIGGER, BTN_THUMB, BTN_Z, BTN_START };
	CHECK(renumberingOf(joystick, oldStart, newStart).empty());
	CHECK_EQ(oldStart, -1);
}
#endif
