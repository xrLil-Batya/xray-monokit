#include "stdafx.h"

#include "DiscordRPC.hpp"
#include "IGame_Persistent.h"

constexpr const char* DISCORD_LIBRARY_DLL{ "discord-rpc.dll" };

ENGINE_API DiscordRPC Discord;

void DiscordRPC::Init()
{
	if(g_dedicated_server)
		return;
	
	m_hDiscordDLL = LoadLibrary(DISCORD_LIBRARY_DLL);
	if (!m_hDiscordDLL)
	{
		Msg("!![%s]Failed to load [%s], error: %s", __FUNCTION__, DISCORD_LIBRARY_DLL, Debug.error2string(GetLastError()));
		return;
	}

	Discord_Initialize = (pDiscord_Initialize)GetProcAddress(m_hDiscordDLL, "Discord_Initialize");
	Discord_Shutdown = (pDiscord_Shutdown)GetProcAddress(m_hDiscordDLL, "Discord_Shutdown");
	Discord_ClearPresence = (pDiscord_ClearPresence)GetProcAddress(m_hDiscordDLL, "Discord_ClearPresence");
	Discord_RunCallbacks = (pDiscord_RunCallbacks)GetProcAddress(m_hDiscordDLL, "Discord_RunCallbacks");
	Discord_UpdatePresence = (pDiscord_UpdatePresence)GetProcAddress(m_hDiscordDLL, "Discord_UpdatePresence");

	if (!Discord_Initialize || !Discord_Shutdown || !Discord_ClearPresence || !Discord_RunCallbacks || !Discord_UpdatePresence) {
		Msg("!![%s] Initialization failed!", __FUNCTION__);
		FreeLibrary(m_hDiscordDLL);
		m_hDiscordDLL = nullptr;
		return;
	}

	DiscordEventHandlers nullHandlers{};
	Discord_Initialize("1008436696871862373", &nullHandlers, TRUE, nullptr);

	start_time = time(nullptr);
	Update();
}

DiscordRPC::~DiscordRPC()
{
	if(g_dedicated_server)
		return;
	
	if (!m_hDiscordDLL) return;

	Discord_ClearPresence();
	Discord_Shutdown();

	FreeLibrary(m_hDiscordDLL);
}


void DiscordRPC::Update()
{
	if (!m_hDiscordDLL) return;

	DiscordRichPresence presenseInfo{};
	presenseInfo.endTimestamp = start_time; //время с момента запуска
	presenseInfo.largeImageKey = "2"; //большая картинка
	presenseInfo.smallImageKey = "1"; //маленькая картинка
	presenseInfo.smallImageText = "Online"; //версия движка на маленькой картинке
	presenseInfo.largeImageText = "«R.A.D» Multiplayer Beta"; //название уровня + активное задание на большой картинке
	presenseInfo.state = "Servers: USA, Europe, Russia"; //Активное задание
	presenseInfo.details = "Gunslinger, VC, Accounts, etc."; //название уровня

	Discord_UpdatePresence(&presenseInfo);
}