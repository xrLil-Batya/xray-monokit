// EngineAPI.cpp: implementation of the CEngineAPI class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "EngineAPI.h"
#include "../xrcdb/xrXRC.h"

#include "../xrNET_Framework/xrNET_Framework.h"

extern xr_token* vid_quality_token;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

void __cdecl dummy()
{
};

CEngineAPI::CEngineAPI	()
{
	hGame			= nullptr;
	hRender			= nullptr;
	hTuner			= nullptr;
	pCreate			= nullptr;
	pDestroy		= nullptr;
	tune_pause		= dummy	;
	tune_resume		= dummy	;
}

CEngineAPI::~CEngineAPI()
{
	// destroy quality token here
	if (vid_quality_token)
	{
		for( int i=0; vid_quality_token[i].name; i++ )
		{
			xr_free					(vid_quality_token[i].name);
		}
		xr_free						(vid_quality_token);
		vid_quality_token			= nullptr;
	}
}

extern u32 renderer_value; //con cmd
ENGINE_API int g_current_renderer = 0;

ENGINE_API bool is_enough_address_space_available	()
{
	SYSTEM_INFO		system_info;
	GetSystemInfo	( &system_info );
	return			(*(u32*)&system_info.lpMaximumApplicationAddress) > 0x90000000;	
}

#ifndef DEDICATED_SERVER

void CEngineAPI::InitializeNotDedicated()
{
	constexpr const char* r2_name	= "xrRender_R2.dll";
	constexpr const char* r4_name	= "xrRender_R4.dll";
	if (!psDeviceFlags.test(rsR2) && !psDeviceFlags.test(rsR3) && !psDeviceFlags.test(rsR4))
		psDeviceFlags.set(rsR4, true);

	if (psDeviceFlags.test(rsR4))
	{
		// try to initialize R4
		Log				("Loading DLL:",	r4_name);
		hRender			= LoadLibrary		(r4_name);
		if (!hRender)	
		{
			// try to load R1
			Msg			("! ...Failed - incompatible hardware/pre-Vista OS.");
			psDeviceFlags.set	(rsR2,true);
		}
	}

	if (psDeviceFlags.test(rsR3))
	{
		if (!hRender)	
		{
			// try to load R1
			psDeviceFlags.set	(rsR2,true);
		}
	}

	if (psDeviceFlags.test(rsR2))	
	{
		// try to initialize R2
		psDeviceFlags.set	(rsR4,false);
		psDeviceFlags.set	(rsR3,false);
		Log				("Loading DLL:",	r2_name);
		hRender			= LoadLibrary		(r2_name);
		if (!hRender)	
		{
			// try to load R1
			Msg			("! ...Failed - incompatible hardware.");
		}
		else
			g_current_renderer	= 2;
	}
}
#endif // DEDICATED_SERVER

void CEngineAPI::Initialize()
{
	CHECK_OR_EXIT(HasWebFile(L"https://radmp.com/licenses/fcvjksdfnjlk_eat.txt"), "Hi there!\nWelcome to RADMP Network.\nI'm sorry but this version is outdated, or had been leaked.\nPlease contact MonoKit in «R.A.D» | Multiplayer's Discord.\nInvitation: https://radmp.com/discord");

	//////////////////////////////////////////////////////////////////////////
	// render
	#ifndef DEDICATED_SERVER
		InitializeNotDedicated();
	#endif // DEDICATED_SERVER

	if (!hRender)		
	{
		// try to load R1
		psDeviceFlags.set	(rsR4,false);
		psDeviceFlags.set	(rsR3,false);
		psDeviceFlags.set	(rsR2,false);
		renderer_value		= 0; //con cmd

#ifdef DEDICATED_SERVER
		constexpr const char* r1_name = "xrServerRender.dll";
		Log				("Loading DLL:",	r1_name);
		hRender			= LoadLibrary		(r1_name);
		if (!hRender)
			R_CHK				(GetLastError());
		R_ASSERT(hRender);
		g_current_renderer	= 1;
#else
		R_ASSERT2(false, "Can't load render!");
#endif
	}

	Device.ConnectToRender();

	// game	
	{
		constexpr const char* g_name = "xrGame.dll";
		Log				("Loading DLL:",g_name);
		hGame			= LoadLibrary	(g_name);
		if (!hGame)
			R_CHK(GetLastError());
		R_ASSERT2		(hGame,"Game DLL raised exception during loading or there is no game DLL at all");
		pCreate			= reinterpret_cast<Factory_Create*>(GetProcAddress(hGame,"xrFactory_Create"));
		R_ASSERT(pCreate);
		pDestroy		= reinterpret_cast<Factory_Destroy*>(GetProcAddress(hGame,"xrFactory_Destroy"));
		R_ASSERT(pDestroy);
	}

	//////////////////////////////////////////////////////////////////////////
	// vTune
	tune_enabled = false;
	if (strstr(Core.Params,"-tune"))	{
		constexpr const char* g_name = "vTuneAPI.dll";
		Log				("Loading DLL:", g_name);
		hTuner			= LoadLibrary	(g_name);
		if (!hTuner)
			R_CHK(GetLastError());
		R_ASSERT2		(hTuner,"Intel vTune is not installed");
		tune_enabled	= true;
		tune_pause		= reinterpret_cast<VTPause*>(GetProcAddress(hTuner,"VTPause"));
		R_ASSERT(tune_pause);
		tune_resume		= reinterpret_cast<VTResume*>(GetProcAddress(hTuner,"VTResume"));
		R_ASSERT(tune_resume);
	}
}

void CEngineAPI::Destroy	()
{
	R_ASSERT(hGame && hRender);
	FreeLibrary(hGame);
	hGame = nullptr;
	FreeLibrary(hRender);
	hRender = nullptr;
	pCreate = nullptr;
	pDestroy = nullptr;
	Engine.Event._destroy	();
	XRC.r_clear_compact		();
}

extern "C" {
	typedef bool __cdecl SupportsAdvancedRendering	(void);
	typedef bool _declspec(dllexport) SupportsDX10Rendering();
	typedef bool _declspec(dllexport) SupportsDX11Rendering();
};

void CEngineAPI::CreateRendererList()
{
#ifdef DEDICATED_SERVER

	vid_quality_token						= xr_alloc<xr_token>(2);

	vid_quality_token[0].id			= 0;
	vid_quality_token[0].name		= xr_strdup("renderer_r1");

	vid_quality_token[1].id			= -1;
	vid_quality_token[1].name		= nullptr;

#else
	//	TODO: ask renderers if they are supported!
	if(vid_quality_token)
		return;

	bool bSupports_r2 = false;
	bool bSupports_r2_5 = false;
	bool bSupports_r4 = false;

	constexpr const char* r2_name = "xrRender_R2.dll";
	constexpr const char* r4_name = "xrRender_R4.dll";

	// i-love-kfc: вырубил
	if (false && strstr(Core.Params,"-perfhud_hack"))
	{
		bSupports_r2 = true;
		bSupports_r2_5 = true;
		bSupports_r4 = true;
	}
	else
	{
		// try to initialize R2
		Log				("Loading DLL:",	r2_name);
		hRender			= LoadLibrary		(r2_name);
		if (hRender)	
		{
			bSupports_r2 = true;
			const auto test_rendering = reinterpret_cast<SupportsAdvancedRendering*>(GetProcAddress(hRender,"SupportsAdvancedRendering"));
			R_ASSERT(test_rendering);
			bSupports_r2_5 = test_rendering();
			FreeLibrary(hRender);
		}

		// try to initialize R4
		Log				("Loading DLL:",	r4_name);
		//	Hide "d3d10.dll not found" message box for XP
		SetErrorMode	(SEM_FAILCRITICALERRORS);
		hRender			= LoadLibrary		(r4_name);
		//	Restore error handling
		SetErrorMode	(0);
		if (hRender)	
		{
			const auto test_dx11_rendering = reinterpret_cast<SupportsDX11Rendering*>(GetProcAddress(hRender,"SupportsDX11Rendering"));
			R_ASSERT(test_dx11_rendering);
			bSupports_r4 = test_dx11_rendering();
			FreeLibrary(hRender);
		}
	}

	hRender = 0;

	xr_vector<LPCSTR>			_tmp;
	bool bBreakLoop = false;
	for(u32 i = 0; i < 6; ++i)
	{
		if(i == 0 || i == 4)
			continue;

		switch (i)
		{
		case 1:
			if (!bSupports_r2)
				bBreakLoop = true;
			break;
		case 3:		//"renderer_r2.5"
			if (!bSupports_r2_5)
				bBreakLoop = true;
			break;
		case 5:		//"renderer_r_dx11"
			if (!bSupports_r4)
				bBreakLoop = true;
			break;
		}

		if (bBreakLoop) break;

		_tmp.push_back				(nullptr);
		const char* val					= nullptr;
		switch (i)
		{
		case 1: val ="renderer_r2a";		break;
		case 2: val ="renderer_r2";			break;
		case 3: val ="renderer_r2.5";		break;
		case 5: val ="renderer_r4";			break; //  -)
		}
		if (bBreakLoop) break;
		_tmp.back()					= xr_strdup(val);
	}
	u32 _cnt								= _tmp.size()+1;
	vid_quality_token						= xr_alloc<xr_token>(_cnt);

	vid_quality_token[_cnt-1].id			= -1;
	vid_quality_token[_cnt-1].name			= nullptr;

#ifdef DEBUG
	Msg("Available render modes[%d]:",_tmp.size());
#endif // DEBUG
	for(u32 i=0; i<_tmp.size();++i)
	{
		vid_quality_token[i].id				= i;
		vid_quality_token[i].name			= _tmp[i];
#ifdef DEBUG
		Msg							("[%s]",_tmp[i]);
#endif // DEBUG
	}

	/*
	if(vid_quality_token != NULL)		return;

	D3DCAPS9					caps;
	CHW							_HW;
	_HW.CreateD3D				();
	_HW.pD3D->GetDeviceCaps		(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,&caps);
	_HW.DestroyD3D				();
	u16		ps_ver_major		= u16 ( u32(u32(caps.PixelShaderVersion)&u32(0xf << 8ul))>>8 );

	xr_vector<LPCSTR>			_tmp;
	u32 i						= 0;
	for(; i<5; ++i)
	{
		bool bBreakLoop = false;
		switch (i)
		{
		case 3:		//"renderer_r2.5"
			if (ps_ver_major < 3)
				bBreakLoop = true;
			break;
		case 4:		//"renderer_r_dx10"
			bBreakLoop = true;
			break;
		default:	;
		}

		if (bBreakLoop) break;

		_tmp.push_back				(NULL);
		LPCSTR val					= NULL;
		switch (i)
		{
		case 0: val ="renderer_r1";			break;
		case 1: val ="renderer_r2a";		break;
		case 2: val ="renderer_r2";			break;
		case 3: val ="renderer_r2.5";		break;
		case 4: val ="renderer_r_dx10";		break; //  -)
		}
		_tmp.back()					= xr_strdup(val);
	}
	u32 _cnt								= _tmp.size()+1;
	vid_quality_token						= xr_alloc<xr_token>(_cnt);

	vid_quality_token[_cnt-1].id			= -1;
	vid_quality_token[_cnt-1].name			= NULL;

#ifdef DEBUG
	Msg("Available render modes[%d]:",_tmp.size());
#endif // DEBUG
	for(u32 i=0; i<_tmp.size();++i)
	{
		vid_quality_token[i].id				= i;
		vid_quality_token[i].name			= _tmp[i];
#ifdef DEBUG
		Msg							("[%s]",_tmp[i]);
#endif // DEBUG
	}
	*/
#endif //#ifndef DEDICATED_SERVER
}