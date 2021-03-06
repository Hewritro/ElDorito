#include "Ui.hpp"

#include "../ElDorito.hpp"
#include "../Patch.hpp"
#include "../Blam/BlamInput.hpp"
#include "../Blam/Tags/ChudGlobalsDefinition.hpp"
#include "../Blam/Tags/ChudDefinition.hpp"
#include "../Blam/BlamNetwork.hpp"
#include "../Menu.hpp"

namespace
{
	void __fastcall UI_MenuUpdateHook(void* a1, int unused, int menuIdToLoad);

	int UI_ShowHalo3PauseMenu(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);
	void UI_EndGame();
	char __fastcall UI_Forge_ButtonPressHandlerHook(void* a1, int unused, uint8_t* controllerStruct);
	void LocalizedStringHook();
	void LobbyMenuButtonHandlerHook();
	void WindowTitleSprintfHook(char* destBuf, char* format, char* version);
	bool MainMenuCreateLobbyHook(int lobbyType);
	void ResolutionChangeHook();
	void __fastcall UI_UpdateRosterColorsHook(void *thisPtr, int unused, void *a0);

	Patch unused; // for some reason a patch field is needed here (on release builds) otherwise the game crashes while loading map/game variants, wtf?
}

namespace Patches
{
	namespace Ui
	{
		bool DialogShow; // todo: put this somewhere better
		unsigned int DialogStringId;
		int DialogArg1; // todo: figure out a better name for this
		int DialogFlags;
		unsigned int DialogParentStringId;
		void* UIData = 0;

		void Tick()
		{
			if (DialogShow)
			{
				if (!UIData) // the game can also free this mem at any time afaik, but it also looks like it resets this ptr to 0, so we can just alloc it again
				{
					typedef void*(__cdecl * UI_AllocPtr)(int size);
					auto UI_Alloc = reinterpret_cast<UI_AllocPtr>(0xAB4ED0);
					UIData = UI_Alloc(0x40);
				}

				// fill UIData with proper data
				typedef void*(__thiscall * UI_OpenDialogByIdPtr)(void* a1, unsigned int dialogStringId, int a3, int dialogFlags, unsigned int parentDialogStringId);
				auto UI_OpenDialogById = reinterpret_cast<UI_OpenDialogByIdPtr>(0xA92780);
				UI_OpenDialogById(UIData, DialogStringId, DialogArg1, DialogFlags, DialogParentStringId);

				// post UI message
				typedef int(*UI_PostMessagePtr)(void* uiDataStruct);
				auto UI_PostMessage = reinterpret_cast<UI_PostMessagePtr>(0xA93450);
				UI_PostMessage(UIData);

				DialogShow = false;
			}
		}

		void ApplyAll()
		{
			// Rewire $hq.MatchmakingLeaveQueue() to end the game
			Hook(0x3B6826, UI_EndGame, HookFlags::IsCall).Apply();
			Patch::NopFill(Pointer::Base(0x3B6826 + 5), 1);

			// Rewire $hf2pEngine.PerformLogin() to show the pause menu
			Hook(0x234756, &UI_ShowHalo3PauseMenu, HookFlags::IsCall).Apply();
			Patch::NopFill(Pointer::Base(0x234756 + 5), 1);

			// Allows you to press B to close the H3 pause menu
			// TODO: find out what the byte that's being checked does, we're just patching out the check here but maybe it's important
			Patch::NopFill(Pointer::Base(0x6E05F3), 2);

			// Fix "Network" setting in lobbies (change broken 0x100B7 menuID to 0x100B6)
			Patch(0x6C34B0, { 0xB6 }).Apply();

			// Fix gamepad option in settings (todo: find out why it doesn't detect gamepads
			// and find a way to at least enable pressing ESC when gamepad is enabled)
			Patch::NopFill(Pointer::Base(0x20D7F2), 2);

			// Fix menu update code to include missing mainmenu code
			Hook(0x6DFB73, &UI_MenuUpdateHook, HookFlags::IsCall).Apply();

			// Sorta hacky way of getting game options screen to show when you press X on lobby
			// TODO: find real way of showing the [X] Edit Game Options text, that might enable it to work without patching
			Hook(0x721B8A, LobbyMenuButtonHandlerHook, HookFlags::IsJmpIfEqual).Apply();

			// Hook UI vftable's forge menu button handler, so arrow keys can act as bumpers
			// added side effect: analog stick left/right can also navigate through menus
			DWORD temp;
			DWORD temp2;
			auto writeAddr = Pointer(0x169EFD8);
			if (!VirtualProtect(writeAddr, 4, PAGE_READWRITE, &temp))
			{
				std::stringstream ss;
				ss << "Failed to set protection on memory address " << std::hex << (void*)writeAddr;
				OutputDebugString(ss.str().c_str());
			}
			else
			{
				writeAddr.Write<uint32_t>((uint32_t)&UI_Forge_ButtonPressHandlerHook);
				VirtualProtect(writeAddr, 4, temp, &temp2);
			}

			// Remove Xbox Live from the network menu
			Patch::NopFill(Pointer::Base(0x723D85), 0x17);
			Pointer::Base(0x723DA1).Write<uint8_t>(0);
			Pointer::Base(0x723DB8).Write<uint8_t>(1);
			Patch::NopFill(Pointer::Base(0x723DFF), 0x3);
			Pointer::Base(0x723E07).Write<uint8_t>(0);
			Pointer::Base(0x723E1C).Write<uint8_t>(0);

			// Localized string override hook
			Hook(0x11E040, LocalizedStringHook).Apply();

			// Remove "BUILT IN" text when choosing map/game variants by feeding the UI_SetVisiblityOfElement func a nonexistant string ID for the element (0x202E8 instead of 0x102E8)
			// TODO: find way to fix this text instead of removing it, since the 0x102E8 element (subitem_edit) is used for other things like editing/viewing map variant metadata
			Patch(0x705D6F, { 0x2 }).Apply();

			// Hook window title sprintf to replace the dest buf with our string
			Hook(0x2EB84, WindowTitleSprintfHook, HookFlags::IsCall).Apply();

			// Hook the call to create a lobby from the main menu so that we
			// can show the server browser if matchmaking is selected
			Hook(0x6E79A7, MainMenuCreateLobbyHook, HookFlags::IsCall).Apply();

			// Runs when the game's resolution is changed
			Hook(0x621303, ResolutionChangeHook, HookFlags::IsCall).Apply();

			// Enable H3UI scaling
			Patch::NopFill(Pointer::Base(0x61FAD1), 2);

			// Change the way that Forge handles dpad up so that it doesn't mess with key repeat
			// Comparing the action tick count to 1 instead of using the "handled" flag does roughly the same thing and lets the menu UI read the key too
			Patch(0x19F17F, { 0x75 }).Apply();
			Patch::NopFill(Pointer::Base(0x19F198), 4);

			// Reimplement the function that assigns lobby roster colors
			Hook(0x726100, UI_UpdateRosterColorsHook).Apply();
		}

		void ApplyMapNameFixes()
		{
			uint32_t levelsGlobalPtr = Pointer::Base(0x149E2E0).Read<uint32_t>();
			if (!levelsGlobalPtr)
				return;

			// TODO: map out these global arrays, content items seems to use same format

			uint32_t numLevels = Pointer(levelsGlobalPtr + 0x34).Read<uint32_t>();

			const wchar_t* search[12] = { L"guardian", L"riverworld", L"s3d_avalanche", L"s3d_edge", L"s3d_reactor", L"s3d_turf", L"cyberdyne", L"chill", L"deadlock", L"bunkerworld", L"shrine", L"zanzibar" };
			const wchar_t* names[12] = { L"Guardian", L"Valhalla", L"Diamondback", L"Edge", L"Reactor", L"Icebox", L"The Pit", L"Narrows", L"High Ground", L"Standoff", L"Sandtrap", L"Last Resort" };
			// TODO: Get names/descs using string ids? Seems the unic tags have descs for most of the maps
			const wchar_t* descs[12] = {
				L"Millennia of tending has produced trees as ancient as the Forerunner structures they have grown around. 2-6 players.",
				L"The crew of V-398 barely survived their unplanned landing in this gorge...this curious gorge. 6-16 players.",
				L"Hot winds blow over what should be a dead moon. A reminder of the power Forerunners once wielded. 6-16 players.",
				L"The remote frontier world of Partition has provided this ancient databank with the safety of seclusion. 6-16 players.",
				L"Being constructed just prior to the Invasion, its builders had to evacuate before it was completed. 6-16 players.",
				L"Downtown Tyumen's Precinct 13 offers an ideal context for urban combat training. 4-10 players.",
				L"Software simulations are held in contempt by the veteran instructors who run these training facilities. 4-10 players.",
				L"Without cooling systems such as these, excess heat from the Ark's forges would render the construct uninhabitable. 2-8 players.",
				L"A relic of older conflicts, this base was reactivated after the New Mombasa Slipspace Event. 4-12 players.",
				L"Once, nearby telescopes listened for a message from the stars. Now, these silos contain our prepared response. 4-12 players.",
				L"Although the Brute occupiers have been driven from this ancient structure, they left plenty to remember them by. 6-16 players",
				L"Remote industrial sites like this one are routinely requisitioned and used as part of Spartan training exercises. 4-12 players."

			};
			for (uint32_t i = 0; i < numLevels; i++)
			{
				Pointer levelNamePtr = Pointer(levelsGlobalPtr + 0x54 + (0x360 * i) + 0x8);
				Pointer levelDescPtr = Pointer(levelsGlobalPtr + 0x54 + (0x360 * i) + 0x8 + 0x40);

				wchar_t levelName[0x21] = { 0 };
				levelNamePtr.Read(levelName, sizeof(wchar_t) * 0x20);

				for (uint32_t y = 0; y < sizeof(search) / sizeof(*search); y++)
				{
					if (wcscmp(search[y], levelName) == 0)
					{
						memset(levelNamePtr, 0, sizeof(wchar_t) * 0x20);
						wcscpy_s(levelNamePtr, 0x20, names[y]);

						memset(levelDescPtr, 0, sizeof(wchar_t) * 0x80);
						wcscpy_s(levelDescPtr, 0x80, descs[y]);
						break;
					}
				}
			}
		}


		void ApplyUIResolution() 
		{
			int* gameResolution = reinterpret_cast<int*>(0x19106C0);
			Blam::Tags::ChudGlobalsDefinition* globals = Blam::Tags::GetTag<Blam::Tags::ChudGlobalsDefinition>(0x01BD);

			// Make UI match it's original width of 1920 pixels on non-widescreen monitors.
			// Fixes the visor getting cut off.
			globals->HudGlobals[0].HudAttributes[0].ResolutionWidth = 1920;

			if ((gameResolution[0] / 16 > gameResolution[1] / 9)) {
				// On aspect ratios with a greater width than 16:9 center the UI on the screen
				globals->HudGlobals[0].HudAttributes[0].ResolutionHeight = 1080;
				globals->HudGlobals[0].HudAttributes[0].HorizontalScale = globals->HudGlobals[0].HudAttributes[0].ResolutionWidth / (float)gameResolution[0];
				globals->HudGlobals[0].HudAttributes[0].VerticalScale = globals->HudGlobals[0].HudAttributes[0].ResolutionHeight / (float)gameResolution[1];
			}
			else
			{
				globals->HudGlobals[0].HudAttributes[0].ResolutionHeight = (int)(((float)gameResolution[1] / (float)gameResolution[0]) * globals->HudGlobals[0].HudAttributes[0].ResolutionWidth);
				globals->HudGlobals[0].HudAttributes[0].HorizontalScale = 0;
				globals->HudGlobals[0].HudAttributes[0].VerticalScale = 0;
			}

			// Adjust motion sensor blip to match the UI resolution
			globals->HudGlobals[0].HudAttributes[0].MotionSensorOffsetX = 122.0f;
			globals->HudGlobals[0].HudAttributes[0].MotionSensorOffsetY = (float)(globals->HudGlobals[0].HudAttributes[0].ResolutionHeight - 84);

			// Fix the bottom of the visor
			Blam::Tags::ChudDefinition* chud = Blam::Tags::GetTag<Blam::Tags::ChudDefinition>(0x0C1E);
			chud->HudWidgets[26].PlacementData[0].OffsetY = (((float)globals->HudGlobals[0].HudAttributes[0].ResolutionHeight - 1080) / 2) + 12;

			// Scale H3UI to match the aspect ratio
			int* UIResolution = reinterpret_cast<int*>(0x19106C8);
			UIResolution[0] = 1152;//1152 x 640 resolution
			UIResolution[1] = (int)(((float)gameResolution[1] / (float)gameResolution[0]) * 1152);
		}
	}
}

namespace
{
	void __fastcall UI_MenuUpdateHook(void* a1, int unused, int menuIdToLoad)
	{
		auto& dorito = ElDorito::Instance();
		if (!dorito.GameHasMenuShown && menuIdToLoad == 0x10083)
		{
			dorito.GameHasMenuShown = true;
			dorito.OnMainMenuShown();
		}

		bool shouldUpdate = *(DWORD*)((uint8_t*)a1 + 0x10) >= 0x1E;
		int uiData0x18Value = 1;
		//if (menuIdToLoad == 0x100A8) // TODO1: find what 0x100A8(H3E) stringid is in HO
		//	uiData0x18Value = 5;

		typedef void(__thiscall *UI_MenuUpdatePtr)(void* a1, int menuIdToLoad);
		auto UI_MenuUpdate = reinterpret_cast<UI_MenuUpdatePtr>(0xADF6E0);
		UI_MenuUpdate(a1, menuIdToLoad);

		if (shouldUpdate)
		{
			typedef void*(__cdecl * UI_AllocPtr)(int size);
			auto UI_Alloc = reinterpret_cast<UI_AllocPtr>(0xAB4ED0);
			void* UIData = UI_Alloc(0x3C);

			// fill UIData with proper data
			typedef void*(__thiscall * UI_OpenDialogByIdPtr)(void* a1, unsigned int dialogStringId, int a3, int dialogFlags, unsigned int parentDialogStringId);
			auto UI_OpenDialogById = reinterpret_cast<UI_OpenDialogByIdPtr>(0xA92780);
			UI_OpenDialogById(UIData, menuIdToLoad, 0xFF, 4, 0x1000D);

			// post UI message
			typedef int(*UI_PostMessagePtr)(void* uiDataStruct);
			auto UI_PostMessage = reinterpret_cast<UI_PostMessagePtr>(0xA93450);
			UI_PostMessage(UIData);

			*(uint32_t*)((char*)UIData + 0x18) = uiData0x18Value;
		}
	}

	int UI_ShowHalo3PauseMenu(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
	{
		Patches::Ui::UIData = 0; // hacky fix for h3 pause menu, i think each different DialogParentStringId should have it's own UIData ptr in a std::map or something
								 // so that the games ptr to the UIData ptr is always the same for that dialog parent id

		Patches::Ui::DialogStringId = 0x10084;
		Patches::Ui::DialogArg1 = 0;
		Patches::Ui::DialogFlags = 4;
		Patches::Ui::DialogParentStringId = 0x1000C;
		Patches::Ui::DialogShow = true;

		return 1;
	}

	void UI_EndGame()
	{
		auto session = Blam::Network::GetActiveSession();
		if (!session || !session->IsEstablished())
			return;
		if (session->IsHost())
			Blam::Network::EndGame();
		else
			Blam::Network::LeaveGame();
	}

	char __fastcall UI_Forge_ButtonPressHandlerHook(void* a1, int unused, uint8_t* controllerStruct)
	{
		uint32_t btnCode = *(uint32_t*)(controllerStruct + 0x1C);

		if (btnCode == Blam::Input::eUiButtonCodeLeft || btnCode == Blam::Input::eUiButtonCodeRight ||
			btnCode == Blam::Input::eUiButtonCodeDpadLeft || btnCode == Blam::Input::eUiButtonCodeDpadRight)
		{
			if (btnCode == Blam::Input::eUiButtonCodeLeft || btnCode == Blam::Input::eUiButtonCodeDpadLeft) // analog left / arrow key left
				*(uint32_t*)(controllerStruct + 0x1C) = Blam::Input::eUiButtonCodeLB;

			if (btnCode == Blam::Input::eUiButtonCodeRight || btnCode == Blam::Input::eUiButtonCodeDpadRight) // analog right / arrow key right
				*(uint32_t*)(controllerStruct + 0x1C) = Blam::Input::eUiButtonCodeRB;
		}

		typedef char(__thiscall *UI_Forge_ButtonPressHandler)(void* a1, void* controllerStruct);
		UI_Forge_ButtonPressHandler buttonHandler = (UI_Forge_ButtonPressHandler)0xAE2180;
		return buttonHandler(a1, controllerStruct);
	}

	bool LocalizedStringHookImpl(int tagIndex, int stringId, wchar_t *outputBuffer)
	{
		const size_t MaxStringLength = 0x400;

		switch (stringId)
		{
		case 0x1010A: // start_new_campaign
		{
			// Get the version string, convert it to uppercase UTF-16, and return it
			std::string version = Utils::Version::GetVersionString();
			std::transform(version.begin(), version.end(), version.begin(), toupper);
			std::wstring unicodeVersion(version.begin(), version.end());
			swprintf(outputBuffer, MaxStringLength, L"ELDEWRITO %s", unicodeVersion.c_str());
			return true;
		}
		}
		return false;
	}

	void WindowTitleSprintfHook(char* destBuf, char* format, char* version)
	{
		std::string windowTitle = "ElDewrito | Version: " + Utils::Version::GetVersionString() + " | Build Date: " __DATE__;
		strcpy_s(destBuf, 0x40, windowTitle.c_str());
	}

	__declspec(naked) void LocalizedStringHook()
	{
		__asm
		{
			// Run the hook implementation function and fallback to the original if it returned false
			push ebp
			mov ebp, esp
			push[ebp + 0x10]
			push[ebp + 0xC]
			push[ebp + 0x8]
			call LocalizedStringHookImpl
			add esp, 0xC
			test al, al
			jz fallback

			// Don't run the original function
			mov esp, ebp
			pop ebp
			ret

		fallback:
			// Execute replaced code and jump back to original function
			sub esp, 0x800
			mov edx, 0x51E049
			jmp edx
		}
	}

	__declspec(naked) void LobbyMenuButtonHandlerHook()
	{
		__asm
		{
			// call sub that handles showing game options
			mov ecx, esi
			push [edi+0x10]
			mov eax, 0xB225B0
			call eax
			// jump back to original function
			mov eax, 0xB21B9F
			jmp eax
		}
	}

	void ShowLanBrowser()
	{
		typedef void*(__cdecl * UI_AllocPtr)(int size);
		auto UI_Alloc = reinterpret_cast<UI_AllocPtr>(0xAB4ED0);
		typedef void*(__fastcall* c_load_game_browser_screen_messagePtr)(void *thisPtr, int unused, int arg0, int arg1, int arg2, int arg3, int arg4, int arg5);
		auto c_load_game_browser_screen_message = reinterpret_cast<c_load_game_browser_screen_messagePtr>(0xADE0B0);
		typedef void(*UI_Dialog_QueueLoadPtr)(void *dialog);
		auto UI_Dialog_QueueLoad = reinterpret_cast<UI_Dialog_QueueLoadPtr>(0xA93450);

		auto dialog = UI_Alloc(0x44);
		if (!dialog)
			return;
		dialog = c_load_game_browser_screen_message(dialog, 0, 0x10355, 0, 4, 0x1000C, 0, 0);
		if (dialog)
			UI_Dialog_QueueLoad(dialog);
	}

	bool MainMenuCreateLobbyHook(int lobbyType)
	{
		// If matchmaking is selected, show the server browser instead
		// TODO: Really need to map out the ui_game_mode enum...
		switch (lobbyType)
		{
		case 1: // Matchmaking
			Menu::Instance().setEnabled(true);
			return true;
		case 4: // Theater (rip)
			ShowLanBrowser();
			return true;
		default:
			typedef bool(*CreateLobbyPtr)(int lobbyType);
			auto CreateLobby = reinterpret_cast<CreateLobbyPtr>(0xA7EE70);
			return CreateLobby(lobbyType);
		}
	}

	void ResolutionChangeHook()
	{
		typedef void(__thiscall *ApplyResolutionChangeFunc)();
		ApplyResolutionChangeFunc ApplyResolutionChange = reinterpret_cast<ApplyResolutionChangeFunc>(0xA226D0);
		ApplyResolutionChange();

		// Update the ingame UI's resolution
		Patches::Ui::ApplyUIResolution();
	}

	int UI_ListItem_GetIndex(void *item)
	{
		typedef int(__fastcall* UI_ListItem_GetIndexPtr)(void *thisPtr, int unused);
		auto vtable = *reinterpret_cast<void***>(item);
		auto GetIndex = reinterpret_cast<UI_ListItem_GetIndexPtr>(vtable[6]);
		return GetIndex(item, 0);
	}

	bool UI_OrderedDataSource_Get(void *data, int index, int key, void *result)
	{
		typedef bool(__fastcall* UI_OrderedDataSource_GetPtr)(void *thisPtr, int unused, int index, int key, void *result);
		auto vtable = *reinterpret_cast<void***>(data);
		auto Get = reinterpret_cast<UI_OrderedDataSource_GetPtr>(vtable[11]);
		return Get(data, 0, index, key, result);
	}

	void __fastcall UI_UpdateRosterColorsHook(void *thisPtr, int unused, void *a0)
	{
		typedef void*(__fastcall* UI_GetOrderedDataSourcePtr)(void *thisPtr, int unused);
		auto UI_GetOrderedDataSource = reinterpret_cast<UI_GetOrderedDataSourcePtr>(0xB14FE0);
		typedef void(__fastcall* UI_BaseUpdateListColorsPtr)(void *thisPtr, int unused, void *a0);
		auto UI_BaseUpdateListColors = reinterpret_cast<UI_BaseUpdateListColorsPtr>(0xB16650);
		typedef void*(__fastcall* UI_Widget_FindFirstChildPtr)(void *thisPtr, int unused, int type);
		auto UI_Widget_FindFirstChild = reinterpret_cast<UI_Widget_FindFirstChildPtr>(0xAB8F80);
		typedef void*(__fastcall* UI_ListItem_NextPtr)(void *thisPtr, int unused, bool a0);
		auto UI_ListItem_Next = reinterpret_cast<UI_ListItem_NextPtr>(0xAB9230);
		typedef void*(__fastcall* UI_Widget_FindChildTextPtr)(void *thisPtr, int unused, int name);
		auto UI_Widget_FindChildText = reinterpret_cast<UI_Widget_FindChildTextPtr>(0xAB8AE0);
		typedef void*(__fastcall* UI_Widget_FindChildBitmapPtr)(void *thisPtr, int unused, int name);
		auto UI_Widget_FindChildBitmap = reinterpret_cast<UI_Widget_FindChildBitmapPtr>(0xAB8A40);
		typedef void(*UI_ColorWidgetWithPlayerColorPtr)(void *widget, int colorIndex, bool isTeam);
		auto UI_ColorWidgetWithPlayerColor = reinterpret_cast<UI_ColorWidgetWithPlayerColorPtr>(0xAA4C80);
		typedef void(__fastcall* UI_Widget_MultiplyColorPtr)(void *thisPtr, int unused, float *argbColor);
		auto UI_Widget_MultiplyColor = reinterpret_cast<UI_Widget_MultiplyColorPtr>(0xAB9D70);
		typedef void(*RgbToFloatColorPtr)(uint32_t rgbColor, float *result);
		auto RgbToFloatColor = reinterpret_cast<RgbToFloatColorPtr>(0x521300);

		const auto str_name = 0x2AA;
		const auto str_base_color = 0x10144;
		const auto str_base_color_hilite = 0x103DD;
		const auto str_team = 0x11D;
		const auto str_player_row_type = 0x103CD;

		// Call the base function in c_gui_list_widget
		UI_BaseUpdateListColors(thisPtr, 0, a0);

		// Get the data source to fetch player information from
		auto data = UI_GetOrderedDataSource(thisPtr, 0);
		if (!data)
			return;

		// Get whether teams are enabled by querying the session parameter
		// TODO: Is there a better way of doing this? The code that H3E uses doesn't seem to work anymore...
		static auto teamsEnabled = false; // This is static so that last value of this can be reused if the session closes
		auto session = Blam::Network::GetActiveSession();
		if (session && session->IsEstablished())
			teamsEnabled = session->HasTeams();

		// Loop through each list item widget
		auto item = UI_Widget_FindFirstChild(thisPtr, 0, 5); // 5 = List item
		while (item)
		{
			// Get child widget pointers
			auto nameWidget = UI_Widget_FindChildText(item, 0, str_name);
			auto baseColorWidget = UI_Widget_FindChildBitmap(item, 0, str_base_color);
			auto baseColorHiliteWidget = UI_Widget_FindChildBitmap(item, 0, str_base_color_hilite);

			// Get the list item index needed for data binding
			auto itemIndex = UI_ListItem_GetIndex(item);

			// Only set the color if player_row_type == 0
			auto playerRowType = 0;
			if (UI_OrderedDataSource_Get(data, itemIndex, str_player_row_type, &playerRowType) && playerRowType == 0)
			{
				// If teams are enabled and the player is assigned to a team, then use the team color,
				// otherwise use the primary color
				auto teamIndex = 0;
				if (teamsEnabled && UI_OrderedDataSource_Get(data, itemIndex, str_team, &teamIndex) && teamIndex >= 0)
				{
					if (nameWidget)
						UI_ColorWidgetWithPlayerColor(nameWidget, teamIndex, true);
					if (baseColorWidget)
						UI_ColorWidgetWithPlayerColor(baseColorWidget, teamIndex, true);
					if (baseColorHiliteWidget)
						UI_ColorWidgetWithPlayerColor(baseColorHiliteWidget, teamIndex, true);
				}
				else
				{
					// Pull the primary color directly out of the data source (it's easier)
					auto primaryColor = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(data) + itemIndex * 0x1678 + 0x7F8);
					float primaryColorF[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
					RgbToFloatColor(primaryColor, &primaryColorF[1]);
					if (baseColorWidget)
						UI_Widget_MultiplyColor(baseColorWidget, 0, primaryColorF);
					if (baseColorHiliteWidget)
						UI_Widget_MultiplyColor(baseColorHiliteWidget, 0, primaryColorF);
				}
			}

			// Move to the next item
			item = UI_ListItem_Next(item, 0, true);
		}
	}
}
