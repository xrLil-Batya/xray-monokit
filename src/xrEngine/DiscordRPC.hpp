#pragma once

class ENGINE_API DiscordRPC final
{
	HMODULE m_hDiscordDLL{};

	using DiscordRichPresence = struct {
		const char* state;   /* max 128 bytes */
		const char* details; /* max 128 bytes */
		long startTimestamp;
		long endTimestamp;
		const char* largeImageKey;  /* max 32 bytes */
		const char* largeImageText; /* max 128 bytes */
		const char* smallImageKey;  /* max 32 bytes */
		const char* smallImageText; /* max 128 bytes */
		const char* partyId;        /* max 128 bytes */
		int partySize;
		int partyMax;
		const char* matchSecret;    /* max 128 bytes */
		const char* joinSecret;     /* max 128 bytes */
		const char* spectateSecret; /* max 128 bytes */
		char instance;
	};
	using DiscordUser = struct {
		const char* userId;
		const char* username;
		const char* discriminator;
		const char* avatar;
	};
	using DiscordEventHandlers = struct {
		void (*ready)(const DiscordUser* request);
		void (*disconnected)(int errorCode, const char* message);
		void (*errored)(int errorCode, const char* message);
		void (*joinGame)(const char* joinSecret);
		void (*spectateGame)(const char* spectateSecret);
		void (*joinRequest)(const DiscordUser* request);
	};

	using pDiscord_Initialize = void(*)(const char* applicationId, DiscordEventHandlers* handlers, int autoRegister, const char* optionalSteamId);
	using pDiscord_Shutdown = void(*)();
	using pDiscord_ClearPresence = void(*)();
	using pDiscord_RunCallbacks = void(*)();
	using pDiscord_UpdatePresence = void(*)(const DiscordRichPresence* presence);

	pDiscord_Initialize Discord_Initialize{};
	pDiscord_Shutdown Discord_Shutdown{};
	pDiscord_ClearPresence Discord_ClearPresence{};
	pDiscord_RunCallbacks Discord_RunCallbacks{};
	pDiscord_UpdatePresence Discord_UpdatePresence{};

	long start_time{};

public:
	DiscordRPC() = default;
	~DiscordRPC();

	void Init();
	void Update();
};

extern ENGINE_API DiscordRPC Discord;