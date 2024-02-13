#include <array>
#include <atomic>

#include <pthread.h>

#include <rwcore.h>
#include <texture_runtime.h>
#include <core/log_client.h>

#include <game/menu_handler.h>

bool g_playMultiplayer{false};
void (*g_MainMenuScreen_AddAllItems)(uintptr_t x0);
extern uintptr_t g_gameAddr;
extern std::atomic<bool> g_clientIsRunning;

static uint32_t (*MainMenuScreen_HasCPSave)();

#pragma pack(push, 1)

struct MenuSlot {
    RwTexture* m_buttonTexure;
    const char* m_fepMask;
    void (*m_onPressedCallback)();
};

struct MainMenuScreen {
    std::array<uint8_t, 0x15> m_undefined0;
    bool m_inGameplayScene;
    std::array<uint8_t, 0x42> m_undefined1;
    
    /* A slot index identifier, needed to be increased while adding
     * new items into it! */
    // Count of available entries inside of m_slot array
    uint32_t m_slotMax;
    uint32_t m_slotIndex;
    MenuSlot* m_slot;
};

#pragma pack(pop)

static_assert(offsetof(MainMenuScreen, m_inGameplayScene) == 0x15);
static_assert(offsetof(MainMenuScreen, m_slotMax) == 0x58);
static_assert(offsetof(MainMenuScreen, m_slotIndex) == 0x5c);
static_assert(offsetof(MainMenuScreen, m_slot) == 0x60);

static const char bNameOnResume[]{"menu_mainresume"};
static const char bNameOnPlay[]{"menu_mainplay"};
static const char bNameOnSettings[]{"menu_mainsettings"};

static const char bNameOnBriefs[]{"menu_mainbriefs"};

static const char bNameOnStats[]{"menu_mainstats"};
static const char bNameOnQuit[]{"menu_mainquit"};

static void (*OnResume_buttonPressed)();
static void (*OnStartGame_buttonPressed)();
static void (*OnSettings_buttonPressed)();
static void (*OnBriefs_buttonPressed)();
static void (*OnStats_buttonPressed)();
static void (*OnExit_buttonPressed)();

static void menuOnInit() {}

[[maybe_unused]] static void OnMultiplayer_buttonPressed()
{
    if (g_playMultiplayer)
        return;

    const time_t t = time(nullptr);
    struct tm* tm = localtime(&t);
    std::array<char, 0x1f> timeBf;
    strftime(timeBf.data(), timeBf.size(), "%c", tm);
    g_playMultiplayer = true;
    menuOnInit();

    useriDsp("Menu: multiplayer button selected from menu at %s, enjoy!", timeBf);
}

static void menu_placeButton(const char* buttonName, const char* fep, MainMenuScreen* menu)
{
    auto textureButton = (RwTexture*)g_textureDatabase->textureLoadNew("gta3", buttonName);
    SALOG_ASSERT(textureButton, "Menu: can't build the menu, some texture hasn't found!");

    auto slotPlaceholder{menu->m_slotIndex};
    userdDsp("Menu: menu slot index: %u", slotPlaceholder);
    const uint32_t newSlot = slotPlaceholder + 1;

    static constexpr uint SLOT_MAX_COUNT{8};
    if (!menu->m_slot) {
        userdDsp("Menu: menu slot doesn't exist, allocating 8 slots now!");

        menu->m_slot = new MenuSlot[SLOT_MAX_COUNT];
        // Putting a trap data into it (this has used for debug purposes only!)!
        std::memset(menu->m_slot, 0xf, sizeof(MenuSlot) * SLOT_MAX_COUNT);

        menu->m_slotMax = SLOT_MAX_COUNT;
    }
    SALOG_ASSERT(newSlot < menu->m_slotMax, "Can't use a slot for store a menu item with name: %s", buttonName);

    auto slotPtr = &(menu->m_slot[menu->m_slotIndex++]);

    userdDsp("Menu: free slot selected in addr: %llx", slotPtr);
    slotPtr->m_buttonTexure = textureButton;
    // Our library will live for entire game scenes, this may not be a memory leak
    slotPtr->m_fepMask = fep;

    // Selecting the correct button callback handler
    if (!std::strncmp(buttonName, bNameOnResume, sizeof(bNameOnResume)))
        slotPtr->m_onPressedCallback = OnResume_buttonPressed;
    else if (!std::strncmp(buttonName, bNameOnPlay, sizeof(bNameOnPlay)))
        slotPtr->m_onPressedCallback = OnStartGame_buttonPressed;
    else if (!std::strncmp(buttonName, bNameOnSettings, sizeof(bNameOnSettings)))
        slotPtr->m_onPressedCallback = OnSettings_buttonPressed;
    else if (!std::strncmp(buttonName, bNameOnBriefs, sizeof(bNameOnBriefs)))
        slotPtr->m_onPressedCallback = OnBriefs_buttonPressed;
    else if (!std::strncmp(buttonName, bNameOnStats, sizeof(bNameOnStats)))
        slotPtr->m_onPressedCallback = OnStats_buttonPressed;
    else if (!std::strncmp(buttonName, bNameOnQuit, sizeof(bNameOnQuit)))
        slotPtr->m_onPressedCallback = OnExit_buttonPressed;
}

void MainMenuScreen_AddAllItems(uintptr_t this_x0)
{
    userdDsp("Menu: on (AddAllItems)!");
    *(uintptr_t*)&MainMenuScreen_HasCPSave = g_gameAddr + 0x35a680;

    auto ourInGameMenu{reinterpret_cast<MainMenuScreen*>(this_x0)};
    userdDsp("Menu: menu structure location: %llx", ourInGameMenu);

    *reinterpret_cast<uintptr_t*>(&OnResume_buttonPressed) = g_gameAddr + 0x35a0f8;
    *reinterpret_cast<uintptr_t*>(&OnStartGame_buttonPressed) = g_gameAddr + 0x35a31c;
    *reinterpret_cast<uintptr_t*>(&OnSettings_buttonPressed) = g_gameAddr + 0x35a208;
    // void FlowScreen::OnStats(void)
    *reinterpret_cast<uintptr_t*>(&OnStats_buttonPressed) = g_gameAddr + 0x35a244;
    // void FlowScreen::OnBriefs(void)
    *reinterpret_cast<uintptr_t*>(&OnBriefs_buttonPressed) = g_gameAddr + 0x35a2b0;

    *reinterpret_cast<uintptr_t*>(&OnExit_buttonPressed) = g_gameAddr + 0x35a758;

    useriDsp("Menu: placing on game menu buttons");

    if (__builtin_expect(!ourInGameMenu->m_inGameplayScene, 0)) {
        // We're in the Main Menu, the user can choice between SAMP or MTA
        userdDsp("Menu: placing Main Menu (Resume) button");
        // For place the "Resume button we need to check if there's a Save Game already"
        uint32_t hasSave = MainMenuScreen_HasCPSave();
        if (hasSave & 1)
            menu_placeButton(bNameOnResume, "FEP_RES", ourInGameMenu);

        menu_placeButton(bNameOnPlay, "FEP_STG", ourInGameMenu);
        menu_placeButton(bNameOnSettings, "FEP_OPT", ourInGameMenu);
    } else {
        menu_placeButton(bNameOnResume, "FEP_RES", ourInGameMenu);
        menu_placeButton(bNameOnSettings, "FEP_OPT", ourInGameMenu);
        menu_placeButton(bNameOnStats, "FEH_STA", ourInGameMenu);

        // Checking if the player has new messages, whether yes...
        if (*reinterpret_cast<uint64_t*>(g_gameAddr + 0xca04d0)) {
            // Putting the "Briefs" button
            menu_placeButton(bNameOnBriefs, "FEH_BRI", ourInGameMenu);
        }

        menu_placeButton(bNameOnPlay, "FEP_STG", ourInGameMenu);
    }

    if (ourInGameMenu->m_slotIndex + 1 <= ourInGameMenu->m_slotMax) {
        // This action always needed to be successful completed
        menu_placeButton(bNameOnQuit, "FEP_QUI", ourInGameMenu);
    }
}

