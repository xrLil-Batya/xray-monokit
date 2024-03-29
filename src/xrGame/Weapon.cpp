#include "stdafx.h"
#include "WeaponMagazinedWGrenade.h"
#include "ParticlesObject.h"
#include "entity_alive.h"
#include "inventory_item_impl.h"
#include "inventory.h"
#include "xrserver_objects_alife_items.h"
#include "actor.h"
#include "actoreffector.h"
#include "level.h"
#include "xr_level_controller.h"
#include "game_cl_base.h"
#include "../Include/xrRender/Kinematics.h"
#include "ai_object_location.h"
#include "../xrphysics/mathutils.h"
#include "object_broker.h"
#include "player_hud.h"
#include "gamepersistent.h"
#include "effectorFall.h"
#include "debug_renderer.h"
#include "static_cast_checked.hpp"
#include "clsid_game.h"
#include "weaponBinocularsVision.h"
#include "ui/UIWindow.h"
#include "ui/UIXmlInit.h"
#include "Torch.h"

#define WEAPON_REMOVE_TIME		60000
#define ROTATION_TIME			0.25f

BOOL	b_toggle_weapon_aim		= FALSE;
extern CUIXml*	pWpnScopeXml;

ENGINE_API extern float psHUD_FOV_def;

CWeapon::CWeapon()
{
	SetState				(eHidden);
	SetNextState			(eHidden);
	m_sub_state				= eSubstateReloadBegin;
	m_bTriStateReload		= false;
	SetDefaults				();

	m_Offset.identity		();
	m_StrapOffset.identity	();

	m_iAmmoCurrentTotal		= 0;
	m_BriefInfo_CalcFrame	= 0;

	iAmmoElapsed			= -1;
	iMagazineSize			= -1;
	m_ammoType				= 0;

	eHandDependence			= hdNone;

	m_zoom_params.m_fCurrentZoomFactor			= g_fov;
	m_zoom_params.m_fZoomRotationFactor			= 0.f;
	m_zoom_params.m_pVision						= NULL;
	m_zoom_params.m_pNight_vision				= NULL;

	m_pCurrentAmmo			= NULL;

	m_pFlameParticles2		= NULL;
	m_sFlameParticles2		= NULL;


	m_fCurrentCartirdgeDisp = 1.f;

	m_strap_bone0			= 0;
	m_strap_bone1			= 0;
	m_StrapOffset.identity	();
	m_strapped_mode			= false;
	m_can_be_strapped		= false;
	m_ef_main_weapon_type	= u32(-1);
	m_ef_weapon_type		= u32(-1);
	m_UIScope				= NULL;
	m_set_next_ammoType_on_reload = undefined_ammo_type;
	m_crosshair_inertion	= 0.f;
	m_cur_scope				= NULL;
	m_bRememberActorNVisnStatus = false;

	m_nearwall_last_hud_fov = psHUD_FOV_def;
}

CWeapon::~CWeapon		()
{
	xr_delete				(m_UIScope);
	delete_data				( m_scopes );
}

void CWeapon::Hit					(SHit* pHDS)
{
	inherited::Hit(pHDS);
}



void CWeapon::UpdateXForm	()
{
	if (Device.dwFrame == dwXF_Frame)
		return;

	dwXF_Frame				= Device.dwFrame;

	if (!H_Parent())
		return;

	// Get access to entity and its visual
	CEntityAlive*			E = smart_cast<CEntityAlive*>(H_Parent());
	
	if (!E)
	{
		if (!IsGameTypeSingle())
			UpdatePosition	(H_Parent()->XFORM());

		return;
	}

	const CInventoryOwner	*parent = smart_cast<const CInventoryOwner*>(E);
	if (parent && parent->use_simplified_visual())
		return;

	if (parent->attached(this))
		return;

	IKinematics*			V = smart_cast<IKinematics*>	(E->Visual());
	VERIFY					(V);

	// Get matrices
	int						boneL = -1, boneR = -1, boneR2 = -1;

	// this ugly case is possible in case of a CustomMonster, not a Stalker, nor an Actor
	E->g_WeaponBones		(boneL,boneR,boneR2);

	if (boneR == -1)		return;

	if ((HandDependence() == hd1Hand) || (GetState() == eReload) || (!E->g_Alive()))
		boneL				= boneR2;

	V->CalculateBones		();
	Fmatrix& mL				= V->LL_GetTransform(u16(boneL));
	Fmatrix& mR				= V->LL_GetTransform(u16(boneR));
	// Calculate
	Fmatrix					mRes;
	Fvector					R,D,N;

	D.sub					(mL.c,mR.c);	

	if(fis_zero(D.magnitude())) {
		mRes.set			(E->XFORM());
		mRes.c.set			(mR.c);
	}
	else {		
		D.normalize			();
		R.crossproduct		(mR.j,D);

		N.crossproduct		(D,R);			
		N.normalize			();

		mRes.set			(R,N,D,mR.c);
		mRes.mulA_43		(E->XFORM());
	}

	UpdatePosition			(mRes);
}

void CWeapon::UpdateFireDependencies_internal()
{
	if (Device.dwFrame!=dwFP_Frame) 
	{
		dwFP_Frame			= Device.dwFrame;

		UpdateXForm			();

		if ( GetHUDmode() )
		{
			HudItemData()->setup_firedeps		(m_current_firedeps);
			VERIFY(_valid(m_current_firedeps.m_FireParticlesXForm));
		} else 
		{
			// 3rd person or no parent
			Fmatrix& parent			= XFORM();
			Fvector& fp				= vLoadedFirePoint;
			Fvector& fp2			= vLoadedFirePoint2;
			Fvector& sp				= vLoadedShellPoint;

			parent.transform_tiny	(m_current_firedeps.vLastFP,fp);
			parent.transform_tiny	(m_current_firedeps.vLastFP2,fp2);
			parent.transform_tiny	(m_current_firedeps.vLastSP,sp);
			
			m_current_firedeps.vLastFD.set	(0.f,0.f,1.f);
			parent.transform_dir	(m_current_firedeps.vLastFD);

			m_current_firedeps.m_FireParticlesXForm.set(parent);
			VERIFY(_valid(m_current_firedeps.m_FireParticlesXForm));
		}
	}
}

void CWeapon::ForceUpdateFireParticles()
{
	if ( !GetHUDmode() )
	{//update particlesXFORM real bullet direction

		if (!H_Parent())		return;

		Fvector					p, d; 
		smart_cast<CEntity*>(H_Parent())->g_fireParams	(this, p,d);

		Fmatrix						_pxf;
		_pxf.k						= d;
		_pxf.i.crossproduct			(Fvector().set(0.0f,1.0f,0.0f),	_pxf.k);
		_pxf.j.crossproduct			(_pxf.k,		_pxf.i);
		_pxf.c						= XFORM().c;
		
		m_current_firedeps.m_FireParticlesXForm.set	(_pxf);
	}
}

LPCSTR wpn_scope_def_bone = "wpn_scope";
LPCSTR wpn_silencer_def_bone = "wpn_silencer";
LPCSTR wpn_launcher_def_bone = "wpn_launcher";
void CWeapon::Load		(LPCSTR section)
{
	inherited::Load					(section);
	CShootingObject::Load			(section);

	
	if(pSettings->line_exist(section, "flame_particles_2"))
		m_sFlameParticles2 = pSettings->r_string(section, "flame_particles_2");

	// load ammo classes
	m_ammoTypes.clear	(); 
	LPCSTR				S = pSettings->r_string(section,"ammo_class");
	if (S && S[0]) 
	{
		string128		_ammoItem;
		int				count		= _GetItemCount	(S);
		for (int it=0; it<count; ++it)	
		{
			_GetItem				(S,it,_ammoItem);
			m_ammoTypes.push_back	(_ammoItem);
		}
	}

	iAmmoElapsed		= pSettings->r_s32		(section,"ammo_elapsed"		);
	iMagazineSize		= pSettings->r_s32		(section,"ammo_mag_size"	);
	
	////////////////////////////////////////////////////
	// ��������� ��������

	//������������� ������ �� ����� ������
	u8 rm = READ_IF_EXISTS( pSettings, r_u8, section, "cam_return", 1 );
	cam_recoil.ReturnMode = (rm == 1);
	
	rm = READ_IF_EXISTS( pSettings, r_u8, section, "cam_return_stop", 0 );
	cam_recoil.StopReturn = (rm == 1);

	float temp_f = 0.0f;
	temp_f					= pSettings->r_float( section,"cam_relax_speed" );
	cam_recoil.RelaxSpeed	= _abs( deg2rad( temp_f ) );
	VERIFY( !fis_zero(cam_recoil.RelaxSpeed) );
	if ( fis_zero(cam_recoil.RelaxSpeed) )
	{
		cam_recoil.RelaxSpeed = EPS_L;
	}

	cam_recoil.RelaxSpeed_AI = cam_recoil.RelaxSpeed;
	if ( pSettings->line_exist( section, "cam_relax_speed_ai" ) )
	{
		temp_f						= pSettings->r_float( section, "cam_relax_speed_ai" );
		cam_recoil.RelaxSpeed_AI	= _abs( deg2rad( temp_f ) );
		VERIFY( !fis_zero(cam_recoil.RelaxSpeed_AI) );
		if ( fis_zero(cam_recoil.RelaxSpeed_AI) )
		{
			cam_recoil.RelaxSpeed_AI = EPS_L;
		}
	}
	temp_f						= pSettings->r_float( section, "cam_max_angle" );
	cam_recoil.MaxAngleVert		= _abs( deg2rad( temp_f ) );
	VERIFY( !fis_zero(cam_recoil.MaxAngleVert) );
	if ( fis_zero(cam_recoil.MaxAngleVert) )
	{
		cam_recoil.MaxAngleVert = EPS;
	}
	
	temp_f						= pSettings->r_float( section, "cam_max_angle_horz" );
	cam_recoil.MaxAngleHorz		= _abs( deg2rad( temp_f ) );
	VERIFY( !fis_zero(cam_recoil.MaxAngleHorz) );
	if ( fis_zero(cam_recoil.MaxAngleHorz) )
	{
		cam_recoil.MaxAngleHorz = EPS;
	}
	
	temp_f						= pSettings->r_float( section, "cam_step_angle_horz" );
	cam_recoil.StepAngleHorz	= deg2rad( temp_f );
	
	cam_recoil.DispersionFrac	= _abs( READ_IF_EXISTS( pSettings, r_float, section, "cam_dispersion_frac", 0.7f ) );

	//������������� ������ �� ����� ������ � ������ zoom ==> ironsight or scope
	//zoom_cam_recoil.Clone( cam_recoil ); ==== ������ !!!!!!!!!!
	zoom_cam_recoil.RelaxSpeed		= cam_recoil.RelaxSpeed;
	zoom_cam_recoil.RelaxSpeed_AI	= cam_recoil.RelaxSpeed_AI;
	zoom_cam_recoil.DispersionFrac	= cam_recoil.DispersionFrac;
	zoom_cam_recoil.MaxAngleVert	= cam_recoil.MaxAngleVert;
	zoom_cam_recoil.MaxAngleHorz	= cam_recoil.MaxAngleHorz;
	zoom_cam_recoil.StepAngleHorz	= cam_recoil.StepAngleHorz;

	zoom_cam_recoil.ReturnMode		= cam_recoil.ReturnMode;
	zoom_cam_recoil.StopReturn		= cam_recoil.StopReturn;

	
	if ( pSettings->line_exist( section, "zoom_cam_relax_speed" ) )
	{
		zoom_cam_recoil.RelaxSpeed		= _abs( deg2rad( pSettings->r_float( section, "zoom_cam_relax_speed" ) ) );
		VERIFY( !fis_zero(zoom_cam_recoil.RelaxSpeed) );
		if ( fis_zero(zoom_cam_recoil.RelaxSpeed) )
		{
			zoom_cam_recoil.RelaxSpeed = EPS_L;
		}
	}
	if ( pSettings->line_exist( section, "zoom_cam_relax_speed_ai" ) )
	{
		zoom_cam_recoil.RelaxSpeed_AI	= _abs( deg2rad( pSettings->r_float( section,"zoom_cam_relax_speed_ai" ) ) ); 
		VERIFY( !fis_zero(zoom_cam_recoil.RelaxSpeed_AI) );
		if ( fis_zero(zoom_cam_recoil.RelaxSpeed_AI) )
		{
			zoom_cam_recoil.RelaxSpeed_AI = EPS_L;
		}
	}
	if ( pSettings->line_exist( section, "zoom_cam_max_angle" ) )
	{
		zoom_cam_recoil.MaxAngleVert	= _abs( deg2rad( pSettings->r_float( section, "zoom_cam_max_angle" ) ) );
		VERIFY( !fis_zero(zoom_cam_recoil.MaxAngleVert) );
		if ( fis_zero(zoom_cam_recoil.MaxAngleVert) )
		{
			zoom_cam_recoil.MaxAngleVert = EPS;
		}
	}
	if ( pSettings->line_exist( section, "zoom_cam_max_angle_horz" ) )
	{
		zoom_cam_recoil.MaxAngleHorz	= _abs( deg2rad( pSettings->r_float( section, "zoom_cam_max_angle_horz" ) ) ); 
		VERIFY( !fis_zero(zoom_cam_recoil.MaxAngleHorz) );
		if ( fis_zero(zoom_cam_recoil.MaxAngleHorz) )
		{
			zoom_cam_recoil.MaxAngleHorz = EPS;
		}
	}
	if ( pSettings->line_exist( section, "zoom_cam_step_angle_horz" ) )	{
		zoom_cam_recoil.StepAngleHorz	= deg2rad( pSettings->r_float( section, "zoom_cam_step_angle_horz" ) ); 
	}
	if ( pSettings->line_exist( section, "zoom_cam_dispersion_frac" ) )	{
		zoom_cam_recoil.DispersionFrac	= _abs( pSettings->r_float( section, "zoom_cam_dispersion_frac" ) );
	}

	m_pdm.m_fPDM_disp_base			= pSettings->r_float( section, "PDM_disp_base"			);
	m_pdm.m_fPDM_disp_vel_factor	= pSettings->r_float( section, "PDM_disp_vel_factor"	);
	m_pdm.m_fPDM_disp_accel_factor	= pSettings->r_float( section, "PDM_disp_accel_factor"	);
	m_pdm.m_fPDM_disp_crouch		= pSettings->r_float( section, "PDM_disp_crouch"		);
	m_pdm.m_fPDM_disp_crouch_no_acc	= pSettings->r_float( section, "PDM_disp_crouch_no_acc" );
	m_crosshair_inertion			= READ_IF_EXISTS(pSettings, r_float, section, "crosshair_inertion",	5.91f);
	m_first_bullet_controller.load	(section);
	fireDispersionConditionFactor = pSettings->r_float(section,"fire_dispersion_condition_factor");

// modified by Peacemaker [17.10.08]
//	misfireProbability			  = pSettings->r_float(section,"misfire_probability"); 
//	misfireConditionK			  = READ_IF_EXISTS(pSettings, r_float, section, "misfire_condition_k",	1.0f);
	misfireStartCondition			= pSettings->r_float(section, "misfire_start_condition");
	misfireEndCondition				= READ_IF_EXISTS(pSettings, r_float, section, "misfire_end_condition", 0.f);
	misfireStartProbability			= READ_IF_EXISTS(pSettings, r_float, section, "misfire_start_prob", 0.f);
	misfireEndProbability			= pSettings->r_float(section, "misfire_end_prob");
	conditionDecreasePerShot		= pSettings->r_float(section,"condition_shot_dec"); 
	conditionDecreasePerQueueShot	= READ_IF_EXISTS(pSettings, r_float, section, "condition_queue_shot_dec", conditionDecreasePerShot); 




	vLoadedFirePoint	= pSettings->r_fvector3		(section,"fire_point"		);
	
	if(pSettings->line_exist(section,"fire_point2")) 
		vLoadedFirePoint2= pSettings->r_fvector3	(section,"fire_point2");
	else 
		vLoadedFirePoint2= vLoadedFirePoint;

	// hands
	eHandDependence		= EHandDependence(pSettings->r_s32(section,"hand_dependence"));
	m_bIsSingleHanded	= true;
	if (pSettings->line_exist(section, "single_handed"))
		m_bIsSingleHanded	= !!pSettings->r_bool(section, "single_handed");
	// 
	m_fMinRadius		= pSettings->r_float		(section,"min_radius");
	m_fMaxRadius		= pSettings->r_float		(section,"max_radius");


	// ���������� � ��������� ��������� � �� ������������ � ���������
	m_eScopeStatus			 = (ALife::EWeaponAddonStatus)pSettings->r_s32(section,"scope_status");
	m_eSilencerStatus		 = (ALife::EWeaponAddonStatus)pSettings->r_s32(section,"silencer_status");
	m_eGrenadeLauncherStatus = (ALife::EWeaponAddonStatus)pSettings->r_s32(section,"grenade_launcher_status");

	m_zoom_params.m_bZoomEnabled		= !!pSettings->r_bool(section,"zoom_enabled");
	m_zoom_params.m_fZoomRotateTime		= pSettings->r_float(section,"zoom_rotate_time");

	/*
	if ( m_eScopeStatus == ALife::eAddonAttachable )
	{
		if(pSettings->line_exist(section, "scopes_sect"))		
		{
			LPCSTR str = pSettings->r_string(section, "scopes_sect");
			for(int i = 0, count = _GetItemCount(str); i < count; ++i )	
			{
				string128						scope_section;
				_GetItem						(str, i, scope_section);
				m_scopes.push_back				(scope_section);
			}
		}
		else
		{
			m_scopes.push_back(section);
		}
	}
	else if( m_eScopeStatus == ALife::eAddonPermanent )
	{
		shared_str scope_tex_name			= pSettings->r_string(cNameSect(), "scope_texture");
		m_zoom_params.m_fScopeZoomFactor	= pSettings->r_float( cNameSect(), "scope_zoom_factor");
		if ( !g_dedicated_server )
		{
			m_UIScope				= xr_new<CUIWindow>();
			if(!pWpnScopeXml)
			{
				pWpnScopeXml			= xr_new<CUIXml>();
				pWpnScopeXml->Load		(CONFIG_PATH, UI_PATH, "scopes.xml");
			}
			CUIXmlInit::InitWindow	(*pWpnScopeXml, scope_tex_name.c_str(), 0, m_UIScope);
		}
	}
	*/

	LoadModParams(section);

	bUseAltScope = bLoadAltScopesParams(section);

//	Msg("Load Scopes [%s]", bUseAltScope ? "true" : "false");

	if (!bUseAltScope)
	{
		LoadOriginalScopesParams(section);
	}


    
	if ( m_eSilencerStatus == ALife::eAddonAttachable )
	{
		m_sSilencerName = pSettings->r_string(section,"silencer_name");
		m_iSilencerX = pSettings->r_s32(section,"silencer_x");
		m_iSilencerY = pSettings->r_s32(section,"silencer_y");
	}

    
	if ( m_eGrenadeLauncherStatus == ALife::eAddonAttachable )
	{
		m_sGrenadeLauncherName = pSettings->r_string(section,"grenade_launcher_name");
		m_iGrenadeLauncherX = pSettings->r_s32(section,"grenade_launcher_x");
		m_iGrenadeLauncherY = pSettings->r_s32(section,"grenade_launcher_y");
	}

	UpdateAltScope();
	InitAddons();
	if(pSettings->line_exist(section,"weapon_remove_time"))
		m_dwWeaponRemoveTime = pSettings->r_u32(section,"weapon_remove_time");
	else
		m_dwWeaponRemoveTime = WEAPON_REMOVE_TIME;

	if(pSettings->line_exist(section,"auto_spawn_ammo"))
		m_bAutoSpawnAmmo = pSettings->r_bool(section,"auto_spawn_ammo");
	else
		m_bAutoSpawnAmmo = TRUE;



	m_zoom_params.m_bHideCrosshairInZoom		= true;

	if(pSettings->line_exist(hud_sect, "zoom_hide_crosshair"))
		m_zoom_params.m_bHideCrosshairInZoom = !!pSettings->r_bool(hud_sect, "zoom_hide_crosshair");	

	Fvector			def_dof;
	def_dof.set		(-1,-1,-1);
//	m_zoom_params.m_ZoomDof		= READ_IF_EXISTS(pSettings, r_fvector3, section, "zoom_dof", Fvector().set(-1,-1,-1));
//	m_zoom_params.m_bZoomDofEnabled	= !def_dof.similar(m_zoom_params.m_ZoomDof);

//	m_zoom_params.m_ReloadDof	= READ_IF_EXISTS(pSettings, r_fvector4, section, "reload_dof", Fvector4().set(-1,-1,-1,-1));


	m_bHasTracers			= !!READ_IF_EXISTS(pSettings, r_bool, section, "tracers", true);
	m_u8TracerColorID		= READ_IF_EXISTS(pSettings, r_u8, section, "tracers_color_ID", u8(-1));

	string256						temp;
	for (int i=egdNovice; i<egdCount; ++i) 
	{
		strconcat					(sizeof(temp),temp,"hit_probability_",get_token_name(difficulty_type_token,i));
		m_hit_probability[i]		= READ_IF_EXISTS(pSettings,r_float,section,temp,1.f);
	}

	if (pSettings->line_exist(section, "silencer_bone"))
		m_sWpn_silencer_bone = pSettings->r_string(section, "silencer_bone");
	else
		m_sWpn_silencer_bone = wpn_silencer_def_bone;

	if (pSettings->line_exist(section, "launcher_bone"))
		m_sWpn_launcher_bone = pSettings->r_string(section, "launcher_bone");
	else
		m_sWpn_launcher_bone = wpn_launcher_def_bone;

	auto LoadBoneNames = [](pcstr section, pcstr line, RStringVec& list)
	{
		list.clear();
		if (pSettings->line_exist(section, line))
		{
			pcstr lineStr = pSettings->r_string(section, line);
			for (int j = 0, cnt = _GetItemCount(lineStr); j < cnt; ++j)
			{
				string128 bone_name;
				_GetItem(lineStr, j, bone_name);
				list.push_back(bone_name);
			}
			return true;
		}
		return false;
	};

	// Default shown bones
	LoadBoneNames(section, "def_show_bones", m_defShownBones);

	// Default hidden bones
	LoadBoneNames(section, "def_hide_bones", m_defHiddenBones);

	if (pSettings->line_exist(section, "bones"))
	{
		pcstr ScopeSect = pSettings->r_string(section, "scope_sect");
		for (int i = 0; i < _GetItemCount(ScopeSect); i++)
		{
			string128 scope;
			_GetItem(ScopeSect, i, scope);
			shared_str bone = pSettings->r_string(scope, "bones");
			m_all_scope_bones.push_back(bone);
		}
	}
	
	m_zoom_params.m_bUseDynamicZoom				= READ_IF_EXISTS(pSettings,r_bool,section,"scope_dynamic_zoom",FALSE);
	m_zoom_params.m_sUseZoomPostprocess			= 0;
	m_zoom_params.m_sUseBinocularVision			= 0;

	//////////////// Прыжок ////////////////////
	m_jump_offset[0] = READ_IF_EXISTS(pSettings, r_fvector3, section, "hud_move_jump_offset_pos", (Fvector{ 0.f, 0.05f, 0.03f }));
	m_jump_offset[1] = READ_IF_EXISTS(pSettings, r_fvector3, section, "hud_move_jump_offset_rot", (Fvector{ 0.f, -10.f, -10.f }));
	fJumpMaxTime = READ_IF_EXISTS(pSettings, r_float, section, "jump_transition_time", 0.26f);

	m_fall_offset[0] = READ_IF_EXISTS(pSettings, r_fvector3, section, "hud_move_fall_offset_pos", (Fvector{ 0.f, -0.05f, 0.06f }));
	m_fall_offset[1] = READ_IF_EXISTS(pSettings, r_fvector3, section, "hud_move_fall_offset_rot", (Fvector{ 0.f, 5.f, 0.f }));
	fFallMaxTime = READ_IF_EXISTS(pSettings, r_float, section, "fall_transition_time", 0.6f);

	m_landing_offset[0] = READ_IF_EXISTS(pSettings, r_fvector3, section, "hud_move_landing_offset_pos", (Fvector{ 0.f, -0.2f, 0.03f }));
	m_landing_offset[1] = READ_IF_EXISTS(pSettings, r_fvector3, section, "hud_move_landing_offset_rot", (Fvector{ 0.f, -5.f, 10.f }));
	fLandingMaxTime = READ_IF_EXISTS(pSettings, r_float, section, "landing_transition_time", 0.7f);

	m_landing2_offset[0] = READ_IF_EXISTS(pSettings, r_fvector3, section, "hud_move_landing2_offset_pos", (Fvector{ 0.f, -0.3f, 0.03f }));
	m_landing2_offset[1] = READ_IF_EXISTS(pSettings, r_fvector3, section, "hud_move_landing2_offset_rot", (Fvector{ 0.f, -13.f, 20.f }));
	fLanding2MaxTime = READ_IF_EXISTS(pSettings, r_float, section, "landing2_transition_time", 0.7f);

	if (fJumpMaxTime <= EPS)
		fJumpMaxTime = 0.01f;

	if (fFallMaxTime <= EPS)
		fFallMaxTime = 0.01f;

	if (fLandingMaxTime <= EPS)
		fLandingMaxTime = 0.01f;

	if (fLanding2MaxTime <= EPS)
		fLanding2MaxTime = 0.01f;
	////////////////////////////////////////////
}

void CWeapon::LoadFireParams		(LPCSTR section)
{
	cam_recoil.Dispersion = deg2rad( pSettings->r_float( section,"cam_dispersion" ) ); 
	cam_recoil.DispersionInc = 0.0f;

	if ( pSettings->line_exist( section, "cam_dispersion_inc" ) )	{
		cam_recoil.DispersionInc = deg2rad( pSettings->r_float( section, "cam_dispersion_inc" ) ); 
	}
	
	zoom_cam_recoil.Dispersion		= cam_recoil.Dispersion;
	zoom_cam_recoil.DispersionInc	= cam_recoil.DispersionInc;

	if ( pSettings->line_exist( section, "zoom_cam_dispersion" ) )
	{
		zoom_cam_recoil.Dispersion		= deg2rad( pSettings->r_float( section, "zoom_cam_dispersion" ) ); 
	}

	if ( pSettings->line_exist( section, "zoom_cam_dispersion_inc" ) )	
	{
		zoom_cam_recoil.DispersionInc	= deg2rad( pSettings->r_float( section, "zoom_cam_dispersion_inc" ) ); 
	}

	CShootingObject::LoadFireParams(section);
};



BOOL CWeapon::net_Spawn		(CSE_Abstract* DC)
{
	m_fRTZoomFactor					= m_zoom_params.m_fScopeZoomFactor;
	BOOL bResult					= inherited::net_Spawn(DC);
	CSE_Abstract					*e	= (CSE_Abstract*)(DC);
	CSE_ALifeItemWeapon			    *E	= smart_cast<CSE_ALifeItemWeapon*>(e);

	//iAmmoCurrent					= E->a_current;
	iAmmoElapsed					= E->a_elapsed;
	m_flagsAddOnState				= E->m_addon_flags.get();
	m_ammoType						= E->ammo_type;
	SetState						(E->wpn_state);
	SetNextState					(E->wpn_state);
	m_cur_scope						= E->m_cur_scope;
	
	m_DefaultCartridge.Load(m_ammoTypes[m_ammoType].c_str(), m_ammoType);
	if(iAmmoElapsed) 
	{
		m_fCurrentCartirdgeDisp = m_DefaultCartridge.param_s.kDisp;
		for(int i = 0; i < iAmmoElapsed; ++i) 
			m_magazine.push_back(m_DefaultCartridge);
	}

	UpdateAltScope();
	UpdateAddonsVisibility();
	InitAddons();

	m_dwWeaponIndependencyTime = 0;

	VERIFY((u32)iAmmoElapsed == m_magazine.size());
	m_bAmmoWasSpawned		= false;


	return bResult;
}

void CWeapon::net_Destroy	()
{
	inherited::net_Destroy	();

	//������� ������� ���������
	StopFlameParticles	();
	StopFlameParticles2	();
	StopLight			();
	Light_Destroy		();

	while (m_magazine.size()) m_magazine.pop_back();
}

BOOL CWeapon::IsUpdating()
{	
	bool bIsActiveItem = m_pInventory && m_pInventory->ActiveItem()==this;
	return bIsActiveItem || bWorking;// || IsPending() || getVisible();
}

void CWeapon::net_Export(NET_Packet& P)
{
	inherited::net_Export	(P);

	P.w_float_q8			(GetCondition(),0.0f,1.0f);


	u8 need_upd				= IsUpdating() ? 1 : 0;
	P.w_u8					(need_upd);
	P.w_u16					(u16(iAmmoElapsed));
	P.w_u8					(m_flagsAddOnState);
	P.w_u8					(m_ammoType);
	P.w_u8					((u8)GetState());
	P.w_u8					((u8)IsZoomed());
	P.w_u8					(m_cur_scope);

}

void CWeapon::net_Import(NET_Packet& P)
{
	inherited::net_Import (P);
	
	float _cond;
	P.r_float_q8			(_cond,0.0f,1.0f);
	SetCondition			(_cond);

	u8 flags				= 0;
	P.r_u8					(flags);

	u16 ammo_elapsed = 0;
	P.r_u16					(ammo_elapsed);

	u8						NewAddonState;
	P.r_u8					(NewAddonState);

	m_flagsAddOnState		= NewAddonState;

	UpdateAltScope();
	UpdateAddonsVisibility	();

	u8 ammoType, wstate;
	P.r_u8					(ammoType);
	P.r_u8					(wstate);

	u8 Zoom;
	P.r_u8					((u8)Zoom);

	P.r_u8(m_cur_scope);


	if (H_Parent() && H_Parent()->Remote())
	{
		if (Zoom) OnZoomIn();
		else OnZoomOut();
	};
	switch (wstate)
	{	
	case eFire:
	case eFire2:
	case eSwitch:
	case eReload:
		{
		}break;	
	default:
		{
			if (ammoType >= m_ammoTypes.size())
				Msg("!! Weapon [%d], State - [%d]", ID(), wstate);
			else
			{
				m_ammoType = ammoType;
				SetAmmoElapsed((ammo_elapsed));
			}
		}break;
	}
	
	VERIFY((u32)iAmmoElapsed == m_magazine.size());
}

void CWeapon::save(NET_Packet &output_packet)
{
	inherited::save	(output_packet);
	save_data		(iAmmoElapsed,					output_packet);
	save_data		(m_cur_scope, 					output_packet);
	save_data		(m_flagsAddOnState, 			output_packet);
	save_data		(m_ammoType,					output_packet);
	save_data		(m_zoom_params.m_bIsZoomModeNow,output_packet);
	save_data		(m_bRememberActorNVisnStatus,	output_packet);
}

void CWeapon::load(IReader &input_packet)
{
	inherited::load	(input_packet);
	load_data		(iAmmoElapsed,					input_packet);
	load_data		(m_cur_scope,					input_packet);
	load_data		(m_flagsAddOnState,				input_packet);
	UpdateAddonsVisibility			();
	load_data		(m_ammoType,					input_packet);
	load_data		(m_zoom_params.m_bIsZoomModeNow,input_packet);

	if (m_zoom_params.m_bIsZoomModeNow)	
			OnZoomIn();
		else			
			OnZoomOut();

	load_data		(m_bRememberActorNVisnStatus,	input_packet);
}


void CWeapon::OnEvent(NET_Packet& P, u16 type) 
{
	switch (type)
	{
	case GE_ADDON_CHANGE:
		{
			P.r_u8					(m_flagsAddOnState);
			UpdateAltScope();
			InitAddons();
			UpdateAddonsVisibility();
		}break;

	case GE_WPN_STATE_CHANGE:
		{
			u8				state;
			P.r_u8			(state);
			P.r_u8			(m_sub_state);		
//			u8 NewAmmoType = 
				P.r_u8();
			u8 AmmoElapsed = P.r_u8();
			u8 NextAmmo = P.r_u8();
			if (NextAmmo == undefined_ammo_type)
				m_set_next_ammoType_on_reload = undefined_ammo_type;
			else
				m_set_next_ammoType_on_reload = NextAmmo;

			if (OnClient()) SetAmmoElapsed(int(AmmoElapsed));			
			OnStateSwitch	(u32(state));
		}
		break;
	default:
		{
			inherited::OnEvent(P,type);
		}break;
	}
};

void CWeapon::shedule_Update	(u32 dT)
{
	// Queue shrink
//	u32	dwTimeCL		= Level().timeServer()-NET_Latency;
//	while ((NET.size()>2) && (NET[1].dwTimeStamp<dwTimeCL)) NET.pop_front();	

	// Inherited
	inherited::shedule_Update	(dT);
}

void CWeapon::OnH_B_Independent	(bool just_before_destroy)
{
	RemoveShotEffector			();

	inherited::OnH_B_Independent(just_before_destroy);

	FireEnd						();
	SetPending					(FALSE);
	SwitchState					(eHidden);

	m_strapped_mode				= false;
	m_zoom_params.m_bIsZoomModeNow	= false;
	UpdateXForm					();

	m_nearwall_last_hud_fov = psHUD_FOV_def;

}

void CWeapon::OnH_A_Independent	()
{
	m_dwWeaponIndependencyTime = Level().timeServer();
	inherited::OnH_A_Independent();
	Light_Destroy				();
	UpdateAddonsVisibility		();
};

void CWeapon::OnH_A_Chield		()
{
	inherited::OnH_A_Chield		();
	UpdateAddonsVisibility		();
};

void CWeapon::OnActiveItem ()
{
	//. from Activate
	UpdateAddonsVisibility();
	m_BriefInfo_CalcFrame = 0;

//. Show
	SwitchState					(eShowing);
//-

	inherited::OnActiveItem		();
	//���� �� ����������� � ������ ���� � �����
//.	SetState					(eIdle);
//.	SetNextState				(eIdle);
}

void CWeapon::OnHiddenItem ()
{
	m_BriefInfo_CalcFrame = 0;

	if(IsGameTypeSingle())
		SwitchState(eHiding);
	else
		SwitchState(eHidden);

	OnZoomOut();
	inherited::OnHiddenItem		();

	m_set_next_ammoType_on_reload = undefined_ammo_type;
}

void CWeapon::SendHiddenItem()
{
	if (!CHudItem::object().getDestroy() && m_pInventory)
	{
		// !!! Just single entry for given state !!!
		NET_Packet		P;
		CHudItem::object().u_EventGen		(P,GE_WPN_STATE_CHANGE,CHudItem::object().ID());
		P.w_u8			(u8(eHiding));
		P.w_u8			(u8(m_sub_state));
		P.w_u8			(m_ammoType);
		P.w_u8			(u8(iAmmoElapsed & 0xff));
		P.w_u8			(m_set_next_ammoType_on_reload);
		CHudItem::object().u_EventSend		(P, net_flags(TRUE, TRUE, FALSE, TRUE));
		SetPending		(TRUE);
	}
}


void CWeapon::OnH_B_Chield		()
{
	m_dwWeaponIndependencyTime = 0;
	inherited::OnH_B_Chield		();

	OnZoomOut					();
	m_set_next_ammoType_on_reload = undefined_ammo_type;

	m_nearwall_last_hud_fov = psHUD_FOV_def;
}

extern u32 hud_adj_mode;
bool CWeapon::AllowBore()
{
	return true;
}

void CWeapon::UpdateCL		()
{
	inherited::UpdateCL		();
	UpdateHUDAddonsVisibility();
	//��������� �� ��������
	UpdateLight				();

	//���������� ��������
	UpdateFlameParticles	();
	UpdateFlameParticles2	();

	if(!IsGameTypeSingle())
		make_Interpolation		();
	
	if( (GetNextState()==GetState()) && IsGameTypeSingle() && H_Parent()==Level().CurrentEntity())
	{
		CActor* pActor	= smart_cast<CActor*>(H_Parent());
		if(pActor && !pActor->AnyMove() && this==pActor->inventory().ActiveItem())
		{
			if (hud_adj_mode==0 && 
				GetState()==eIdle && 
				(Device.dwTimeGlobal-m_dw_curr_substate_time>20000) && 
				!IsZoomed()&&
				g_player_hud->attached_item(1)==NULL)
			{
				if(AllowBore())
					SwitchState		(eBore);

				ResetSubStateTime	();
			}
		}
	}

	if(m_zoom_params.m_pNight_vision && !need_renderable())
	{
		if(!m_zoom_params.m_pNight_vision->IsActive())
		{
			CActor *pA = smart_cast<CActor *>(H_Parent());
			R_ASSERT(pA);
			CTorch* pTorch = smart_cast<CTorch*>( pA->inventory().ItemFromSlot(TORCH_SLOT) );
			if ( pTorch && pTorch->GetNightVisionStatus() )
			{
				m_bRememberActorNVisnStatus = pTorch->GetNightVisionStatus();
				pTorch->SwitchNightVision(false, false);
			}
			m_zoom_params.m_pNight_vision->Start(m_zoom_params.m_sUseZoomPostprocess, pA, false);
		}

	}
	else if(m_bRememberActorNVisnStatus)
	{
		m_bRememberActorNVisnStatus = false;
		EnableActorNVisnAfterZoom();
	}

	if(m_zoom_params.m_pVision)
		m_zoom_params.m_pVision->Update();
}
void CWeapon::EnableActorNVisnAfterZoom()
{
	CActor *pA = smart_cast<CActor *>(H_Parent());
	if(IsGameTypeSingle() && !pA)
		pA = g_actor;

	if(pA)
	{
		CTorch* pTorch = smart_cast<CTorch*>( pA->inventory().ItemFromSlot(TORCH_SLOT) );
		if ( pTorch )
		{
			pTorch->SwitchNightVision(true, false);
			pTorch->GetNightVision()->PlaySounds(CNightVisionEffector::eIdleSound);
		}
	}
}

bool  CWeapon::need_renderable()
{
	return !( IsZoomed() && ZoomTexture() && !IsRotatingToZoom() );
}

void CWeapon::renderable_Render		()
{
	UpdateXForm				();

	//���������� ���������

	RenderLight				();	

	//���� �� � ������ ���������, �� ��� HUD �������� �� ����
	if(IsZoomed() && !IsRotatingToZoom() && ZoomTexture())
		RenderHud		(FALSE);
	else
		RenderHud		(TRUE);

	inherited::renderable_Render	();
}

void CWeapon::signal_HideComplete()
{
	if(H_Parent()) 
		setVisible			(FALSE);
	SetPending				(FALSE);
}

void CWeapon::SetDefaults()
{
	SetPending			(FALSE);

	m_flags.set			(FUsingCondition, TRUE);
	bMisfire			= false;
	m_flagsAddOnState	= 0;
	m_zoom_params.m_bIsZoomModeNow	= false;
}

void CWeapon::UpdatePosition(const Fmatrix& trans)
{
	Position().set		(trans.c);
	XFORM().mul			(trans,m_strapped_mode ? m_StrapOffset : m_Offset);
	VERIFY				(!fis_zero(DET(renderable.xform)));
}


bool CWeapon::Action(u16 cmd, u32 flags) 
{
	if(inherited::Action(cmd, flags)) return true;

	
	switch(cmd) 
	{
		case kWPN_FIRE: 
			{
				//���� ������ ���-�� ������, �� ������ �� ������
				{				
					if(IsPending())		
						return				false;

					if(flags&CMD_START) 
						FireStart			();
					else 
						FireEnd				();
				};
			} 
			return true;
		case kWPN_NEXT: 
			{
				return SwitchAmmoType(flags);
			} 

		case kWPN_ZOOM:
			if(IsZoomEnabled())
			{
				if(b_toggle_weapon_aim)
				{
					if(flags&CMD_START)
					{
						if(!IsZoomed())
						{
							if(!IsPending())
							{
								if(GetState()!=eIdle)
									SwitchState(eIdle);
								OnZoomIn	();
							}
						}else
							OnZoomOut	();
					}
				}else
				{
					if(flags&CMD_START)
					{
						if(!IsZoomed() && !IsPending())
						{
							if(GetState()!=eIdle)
								SwitchState(eIdle);
							OnZoomIn	();
						}
					}else 
						if(IsZoomed())
							OnZoomOut	();
				}
				return true;
			}else 
				return false;

		case kWPN_ZOOM_INC:
		case kWPN_ZOOM_DEC:
			if(IsZoomEnabled() && IsZoomed())
			{
				if(cmd==kWPN_ZOOM_INC)  ZoomInc();
				else					ZoomDec();
				return true;
			}else
				return false;
	}
	return false;
}

bool CWeapon::SwitchAmmoType( u32 flags ) 
{
	if ( IsPending() || OnClient() )
		return false;

	if ( !(flags & CMD_START) )
		return false;

	u8 l_newType = m_ammoType;
	bool b1, b2;
	do 
	{
		l_newType = u8( (u32(l_newType+1)) % m_ammoTypes.size() );
		b1 = (l_newType != m_ammoType);
		b2 = unlimited_ammo() ? false : ( !m_pInventory->GetAny( m_ammoTypes[l_newType].c_str() ) );						
	} while( b1 && b2 );

	if ( l_newType != m_ammoType )
	{
		m_set_next_ammoType_on_reload = l_newType;					
		if ( OnServer() )
		{
			Reload();
		}
	}
	return true;
}

void CWeapon::SpawnAmmo(u32 boxCurr, LPCSTR ammoSect, u32 ParentID) 
{
	if(!m_ammoTypes.size())			return;
	if (OnClient())					return;
	m_bAmmoWasSpawned				= true;
	
	int l_type						= 0;
	l_type							%= m_ammoTypes.size();

	if(!ammoSect) ammoSect			= m_ammoTypes[l_type].c_str(); 
	
	++l_type; 
	l_type							%= m_ammoTypes.size();

	CSE_Abstract *D					= F_entity_Create(ammoSect);

	{	
		CSE_ALifeItemAmmo *l_pA		= smart_cast<CSE_ALifeItemAmmo*>(D);
		R_ASSERT					(l_pA);
		l_pA->m_boxSize				= (u16)pSettings->r_s32(ammoSect, "box_size");
		D->s_name					= ammoSect;
		D->set_name_replace			("");
//.		D->s_gameid					= u8(GameID());
		D->s_RP						= 0xff;
		D->ID						= 0xffff;
		if (ParentID == 0xffffffff)	
			D->ID_Parent			= (u16)H_Parent()->ID();
		else
			D->ID_Parent			= (u16)ParentID;

		D->ID_Phantom				= 0xffff;
		D->s_flags.assign			(M_SPAWN_OBJECT_LOCAL);
		D->RespawnTime				= 0;
		l_pA->m_tNodeID				= g_dedicated_server ? u32(-1) : ai_location().level_vertex_id();

		if(boxCurr == 0xffffffff) 	
			boxCurr					= l_pA->m_boxSize;

		while(boxCurr) 
		{
			l_pA->a_elapsed			= (u16)(boxCurr > l_pA->m_boxSize ? l_pA->m_boxSize : boxCurr);
			NET_Packet				P;
			D->Spawn_Write			(P, TRUE);
			Level().Send			(P,net_flags(TRUE));

			if(boxCurr > l_pA->m_boxSize) 
				boxCurr				-= l_pA->m_boxSize;
			else 
				boxCurr				= 0;
		}
	}
	F_entity_Destroy				(D);
}

int CWeapon::GetSuitableAmmoTotal( bool use_item_to_spawn ) const
{
	int ae_count = iAmmoElapsed;
	if ( !m_pInventory )
	{
		return ae_count;
	}

	//���� �� ������ ������ ����������
	if ( m_pInventory->ModifyFrame() <= m_BriefInfo_CalcFrame )
	{
		return ae_count + m_iAmmoCurrentTotal;
	}
	m_BriefInfo_CalcFrame = Device.dwFrame;

	m_iAmmoCurrentTotal = 0;
	for ( u8 i = 0; i < u8(m_ammoTypes.size()); ++i ) 
	{
		m_iAmmoCurrentTotal += GetAmmoCount_forType( m_ammoTypes[i] );

		if ( !use_item_to_spawn )
		{
			continue;
		}
		if ( !inventory_owner().item_to_spawn() )
		{
			continue;
		}
		m_iAmmoCurrentTotal += inventory_owner().ammo_in_box_to_spawn();
	}
	return ae_count + m_iAmmoCurrentTotal;
}

int CWeapon::GetAmmoCount( u8 ammo_type ) const
{
	VERIFY( m_pInventory );
	R_ASSERT( ammo_type < m_ammoTypes.size() );

	return GetAmmoCount_forType( m_ammoTypes[ammo_type] );
}

int CWeapon::GetAmmoCount_forType( shared_str const& ammo_type ) const
{
	int res = 0;

	TIItemContainer::iterator itb = m_pInventory->m_belt.begin();
	TIItemContainer::iterator ite = m_pInventory->m_belt.end();
	for ( ; itb != ite; ++itb ) 
	{
		CWeaponAmmo*	pAmmo = smart_cast<CWeaponAmmo*>( *itb );
		if ( pAmmo && (pAmmo->cNameSect() == ammo_type) )
		{
			res += pAmmo->m_boxCurr;
		}
	}

	itb = m_pInventory->m_ruck.begin();
	ite = m_pInventory->m_ruck.end();
	for ( ; itb != ite; ++itb ) 
	{
		CWeaponAmmo*	pAmmo = smart_cast<CWeaponAmmo*>( *itb );
		if ( pAmmo && (pAmmo->cNameSect() == ammo_type) )
		{
			res += pAmmo->m_boxCurr;
		}
	}
	return res;
}

float CWeapon::GetConditionMisfireProbability() const
{
// modified by Peacemaker [17.10.08]
//	if(GetCondition() > 0.95f) 
//		return 0.0f;
	if(GetCondition() > misfireStartCondition) 
		return 0.0f;
	if(GetCondition() < misfireEndCondition) 
		return misfireEndProbability;
//	float mis = misfireProbability+powf(1.f-GetCondition(), 3.f)*misfireConditionK;
	float mis = misfireStartProbability + (
		(misfireStartCondition - GetCondition()) *				// condition goes from 1.f to 0.f
		(misfireEndProbability - misfireStartProbability) /		// probability goes from 0.f to 1.f
		((misfireStartCondition == misfireEndCondition) ?		// !!!say "No" to devision by zero
			misfireStartCondition : 
			(misfireStartCondition - misfireEndCondition))
										  );
	clamp(mis,0.0f,0.99f);
	return mis;
}

BOOL CWeapon::CheckForMisfire	()
{
	if (OnClient()) return FALSE;

	float rnd = ::Random.randF(0.f,1.f);
	float mp = GetConditionMisfireProbability();
	if(rnd < mp)
	{
		FireEnd();

		bMisfire = true;
		SwitchState(eMisfire);		
		
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

BOOL CWeapon::IsMisfire() const
{	
	return bMisfire;
}
void CWeapon::Reload()
{
	OnZoomOut();
}


bool CWeapon::IsGrenadeLauncherAttached() const
{
	return (ALife::eAddonAttachable == m_eGrenadeLauncherStatus &&
			0 != (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher)) || 
			ALife::eAddonPermanent == m_eGrenadeLauncherStatus;
}

bool CWeapon::IsScopeAttached() const
{
	return (ALife::eAddonAttachable == m_eScopeStatus &&
			0 != (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonScope)) || 
			ALife::eAddonPermanent == m_eScopeStatus;

}

bool CWeapon::IsSilencerAttached() const
{
	return (ALife::eAddonAttachable == m_eSilencerStatus &&
			0 != (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonSilencer)) || 
			ALife::eAddonPermanent == m_eSilencerStatus;
}

bool CWeapon::GrenadeLauncherAttachable()
{
	return (ALife::eAddonAttachable == m_eGrenadeLauncherStatus);
}
bool CWeapon::ScopeAttachable()
{
	return (ALife::eAddonAttachable == m_eScopeStatus);
}
bool CWeapon::SilencerAttachable()
{
	return (ALife::eAddonAttachable == m_eSilencerStatus);
}

void CWeapon::UpdateHUDAddonsVisibility()
{//actor only
	if(!GetHUDmode())										return;

	u16 bone_id = HudItemData()->m_model->LL_BoneID(wpn_scope_def_bone);

	auto SetBoneVisible = [&](const shared_str& boneName, BOOL visibility)
	{
		HudItemData()->set_bone_visible(boneName, visibility, TRUE);
	};

	// Hide default bones
	for (const shared_str& bone : m_defHiddenBones)
	{
		SetBoneVisible(bone, FALSE);
	}

	// Show default bones
	for (const shared_str& bone : m_defShownBones)
	{
		SetBoneVisible(bone, TRUE);
	}

	for (int i = 0; i < m_all_scope_bones.size(); i++)
		SetBoneVisible(m_all_scope_bones[i], FALSE);

	if (m_cur_scope_bone != NULL)
		SetBoneVisible(m_cur_scope_bone, TRUE);

	if (bone_id != BI_NONE) 
	{
		if (ScopeAttachable())
		{
			HudItemData()->set_bone_visible(wpn_scope_def_bone, IsScopeAttached());
		}

		if (m_eScopeStatus == ALife::eAddonDisabled)
		{
			HudItemData()->set_bone_visible(wpn_scope_def_bone, FALSE, TRUE);
		}
	}
	else
		if (m_eScopeStatus == ALife::eAddonPermanent)
			HudItemData()->set_bone_visible(wpn_scope_def_bone, TRUE, TRUE);


	if(SilencerAttachable())
	{
		SetBoneVisible(m_sWpn_silencer_bone, IsSilencerAttached());
	}
	if(m_eSilencerStatus==ALife::eAddonDisabled )
	{
		SetBoneVisible(m_sWpn_silencer_bone, FALSE);
	}
	else
		if(m_eSilencerStatus==ALife::eAddonPermanent)
			SetBoneVisible(m_sWpn_silencer_bone, TRUE);

	if(GrenadeLauncherAttachable())
	{
		SetBoneVisible(m_sWpn_launcher_bone, IsGrenadeLauncherAttached());
	}
	if(m_eGrenadeLauncherStatus==ALife::eAddonDisabled )
	{
		SetBoneVisible(m_sWpn_launcher_bone, FALSE);
	}else
		if(m_eGrenadeLauncherStatus==ALife::eAddonPermanent)
			SetBoneVisible(m_sWpn_launcher_bone, TRUE);
}

void CWeapon::UpdateAddonsVisibility()
{
	IKinematics* pWeaponVisual = smart_cast<IKinematics*>(Visual()); R_ASSERT(pWeaponVisual);

	UpdateHUDAddonsVisibility								();	

	pWeaponVisual->CalculateBones_Invalidate				();

	u16 bone_id = pWeaponVisual->LL_BoneID(wpn_scope_def_bone);

	auto SetBoneVisible = [&](const shared_str& boneName, BOOL visibility)
	{
		u16 bone_id = pWeaponVisual->LL_BoneID(boneName);
		if (bone_id != BI_NONE && visibility != pWeaponVisual->LL_GetBoneVisible(bone_id))
			pWeaponVisual->LL_SetBoneVisible(bone_id, visibility, TRUE);
	};

	// Hide default bones
	for (const shared_str& bone : m_defHiddenBones)
	{
		SetBoneVisible(bone, FALSE);
	}

	// Show default bones
	for (const shared_str& bone : m_defShownBones)
	{
		SetBoneVisible(bone, TRUE);
	}

	for (int i = 0; i < m_all_scope_bones.size(); i++)
		SetBoneVisible(m_all_scope_bones[i], FALSE);

	if (m_cur_scope_bone != NULL)
		SetBoneVisible(m_cur_scope_bone, TRUE);
	
	if(ScopeAttachable() && bone_id != BI_NONE)
	{
		if(IsScopeAttached())
		{
			if(!pWeaponVisual->LL_GetBoneVisible		(bone_id))
				pWeaponVisual->LL_SetBoneVisible				(bone_id,TRUE,TRUE);
		}
		else
		{
			if(pWeaponVisual->LL_GetBoneVisible				(bone_id))
				pWeaponVisual->LL_SetBoneVisible			(bone_id,FALSE,TRUE);
		}
	}

	if(m_eScopeStatus==ALife::eAddonDisabled && bone_id!=BI_NONE &&	pWeaponVisual->LL_GetBoneVisible(bone_id) )
	{
		pWeaponVisual->LL_SetBoneVisible					(bone_id,FALSE,TRUE);
//		Log("scope", pWeaponVisual->LL_GetBoneVisible		(bone_id));
	}

	bone_id = pWeaponVisual->LL_BoneID						(m_sWpn_silencer_bone);
	if(SilencerAttachable())
	{
		if(IsSilencerAttached()){
			if(!pWeaponVisual->LL_GetBoneVisible		(bone_id))
				pWeaponVisual->LL_SetBoneVisible			(bone_id,TRUE,TRUE);
		}else{
			if( pWeaponVisual->LL_GetBoneVisible			(bone_id))
				pWeaponVisual->LL_SetBoneVisible			(bone_id,FALSE,TRUE);
		}
	}
	if(m_eSilencerStatus==ALife::eAddonDisabled && bone_id!=BI_NONE && 
		pWeaponVisual->LL_GetBoneVisible(bone_id) )
	{
		pWeaponVisual->LL_SetBoneVisible					(bone_id,FALSE,TRUE);
//		Log("silencer", pWeaponVisual->LL_GetBoneVisible	(bone_id));
	}

	bone_id = pWeaponVisual->LL_BoneID						(m_sWpn_launcher_bone);
	if(GrenadeLauncherAttachable())
	{
		if(IsGrenadeLauncherAttached())
		{
			if(!pWeaponVisual->LL_GetBoneVisible		(bone_id))
				pWeaponVisual->LL_SetBoneVisible			(bone_id,TRUE,TRUE);
		}else{
			if(pWeaponVisual->LL_GetBoneVisible				(bone_id))
				pWeaponVisual->LL_SetBoneVisible			(bone_id,FALSE,TRUE);
		}
	}
	if(m_eGrenadeLauncherStatus==ALife::eAddonDisabled && bone_id!=BI_NONE && 
		pWeaponVisual->LL_GetBoneVisible(bone_id) )
	{
		pWeaponVisual->LL_SetBoneVisible					(bone_id,FALSE,TRUE);
//		Log("gl", pWeaponVisual->LL_GetBoneVisible			(bone_id));
	}
	

	pWeaponVisual->CalculateBones_Invalidate				();
	pWeaponVisual->CalculateBones							(TRUE);
}


void CWeapon::InitAddons()
{
}

float CWeapon::CurrentZoomFactor()
{
	return IsScopeAttached() ? m_zoom_params.m_fScopeZoomFactor : m_zoom_params.m_fIronSightZoomFactor;
};
void GetZoomData(const float scope_factor, float& delta, float& min_zoom_factor);
void CWeapon::OnZoomIn()
{
	m_zoom_params.m_bIsZoomModeNow		= true;
	if(m_zoom_params.m_bUseDynamicZoom)
		SetZoomFactor(m_fRTZoomFactor);
	else
		m_zoom_params.m_fCurrentZoomFactor	= CurrentZoomFactor();

	EnableHudInertion					(FALSE);

	
	//if(m_zoom_params.m_bZoomDofEnabled && !IsScopeAttached())
	//	GamePersistent().SetEffectorDOF	(m_zoom_params.m_ZoomDof);

	if(GetHUDmode())
		GamePersistent().SetPickableEffectorDOF(true);

	if(m_zoom_params.m_sUseBinocularVision.size() && IsScopeAttached() && NULL==m_zoom_params.m_pVision) 
		m_zoom_params.m_pVision	= xr_new<CBinocularsVision>(m_zoom_params.m_sUseBinocularVision/*"wpn_binoc"*/);

	if(m_zoom_params.m_sUseZoomPostprocess.size() && IsScopeAttached()) 
	{
		CActor *pA = smart_cast<CActor *>(H_Parent());
		if(pA)
		{
			if(NULL==m_zoom_params.m_pNight_vision)
			{
				m_zoom_params.m_pNight_vision	= xr_new<CNightVisionEffector>(m_zoom_params.m_sUseZoomPostprocess/*"device_torch"*/);
			}
		}
	}
}

void CWeapon::OnZoomOut()
{
	m_zoom_params.m_bIsZoomModeNow		= false;
	m_fRTZoomFactor = GetZoomFactor();//store current
	m_zoom_params.m_fCurrentZoomFactor	= g_fov;
	EnableHudInertion					(TRUE);

// 	GamePersistent().RestoreEffectorDOF	();

	if(GetHUDmode())
		GamePersistent().SetPickableEffectorDOF(false);

	ResetSubStateTime					();

	xr_delete							(m_zoom_params.m_pVision);
	if(m_zoom_params.m_pNight_vision)
	{
		m_zoom_params.m_pNight_vision->Stop(100000.0f, false);
		xr_delete(m_zoom_params.m_pNight_vision);
	}
}

CUIWindow* CWeapon::ZoomTexture()
{
	if (UseScopeTexture())
		return m_UIScope;
	else
		return NULL;
}

void CWeapon::SwitchState(u32 S)
{
	if (OnClient()) return;

#ifndef MASTER_GOLD
	if ( bDebug )
	{
		Msg("---Server is going to send GE_WPN_STATE_CHANGE to [%d], weapon_section[%s], parent[%s]",
			S, cNameSect().c_str(), H_Parent() ? H_Parent()->cName().c_str() : "NULL Parent");
	}
#endif // #ifndef MASTER_GOLD

	SetNextState		( S );
	if (CHudItem::object().Local() && !CHudItem::object().getDestroy() && m_pInventory && OnServer())	
	{
		// !!! Just single entry for given state !!!
		NET_Packet		P;
		CHudItem::object().u_EventGen		(P,GE_WPN_STATE_CHANGE,CHudItem::object().ID());
		P.w_u8			(u8(S));
		P.w_u8			(u8(m_sub_state));
		P.w_u8			(m_ammoType);
		P.w_u8			(u8(iAmmoElapsed & 0xff));
		P.w_u8			(m_set_next_ammoType_on_reload);
		CHudItem::object().u_EventSend		(P, net_flags(TRUE, TRUE, FALSE, TRUE));
	}
}

void CWeapon::OnMagazineEmpty	()
{
	VERIFY((u32)iAmmoElapsed == m_magazine.size());
}


void CWeapon::reinit			()
{
	CShootingObject::reinit		();
	CHudItemObject::reinit			();
}

void CWeapon::reload			(LPCSTR section)
{
	CShootingObject::reload		(section);
	CHudItemObject::reload			(section);
	
	m_can_be_strapped			= true;
	m_strapped_mode				= false;
	
	if (pSettings->line_exist(section,"strap_bone0"))
		m_strap_bone0			= pSettings->r_string(section,"strap_bone0");
	else
		m_can_be_strapped		= false;
	
	if (pSettings->line_exist(section,"strap_bone1"))
		m_strap_bone1			= pSettings->r_string(section,"strap_bone1");
	else
		m_can_be_strapped		= false;

	if (m_eScopeStatus == ALife::eAddonAttachable) {
		m_addon_holder_range_modifier	= READ_IF_EXISTS(pSettings,r_float,GetScopeName(),"holder_range_modifier",m_holder_range_modifier);
		m_addon_holder_fov_modifier		= READ_IF_EXISTS(pSettings,r_float,GetScopeName(),"holder_fov_modifier",m_holder_fov_modifier);
	}
	else {
		m_addon_holder_range_modifier	= m_holder_range_modifier;
		m_addon_holder_fov_modifier		= m_holder_fov_modifier;
	}


	{
		Fvector				pos,ypr;
		pos					= pSettings->r_fvector3		(section,"position");
		ypr					= pSettings->r_fvector3		(section,"orientation");
		ypr.mul				(PI/180.f);

		m_Offset.setHPB			(ypr.x,ypr.y,ypr.z);
		m_Offset.translate_over	(pos);
	}

	m_StrapOffset			= m_Offset;

	if (pSettings->line_exist(section,"strap_position") && pSettings->line_exist(section,"strap_orientation")) {
		Fvector				pos,ypr;
		pos					= pSettings->r_fvector3		(section,"strap_position");
		ypr					= pSettings->r_fvector3		(section,"strap_orientation");
		ypr.mul				(PI/180.f);

		m_StrapOffset.setHPB			(ypr.x,ypr.y,ypr.z);
		m_StrapOffset.translate_over	(pos);
	}
	else
		m_can_be_strapped	= false;

	m_ef_main_weapon_type	= READ_IF_EXISTS(pSettings,r_u32,section,"ef_main_weapon_type",u32(-1));
	m_ef_weapon_type		= READ_IF_EXISTS(pSettings,r_u32,section,"ef_weapon_type",u32(-1));
}

void CWeapon::create_physic_shell()
{
	// Временный? "фикс" для оружия из ганслингера, валяющегося на земле. По непонятным причинам (много костей или хз от чего ещё) в некоторых случаях при рассчетах
    // физики происходят краши в ode которые исправить невозможно.
	m_pPhysicsShell = P_build_SimpleShell(this, 0.3f, false);
	m_pPhysicsShell->SetMaterial(smart_cast<IKinematics*>(Visual())->LL_GetData(smart_cast<IKinematics*>(Visual())->LL_GetBoneRoot()).game_mtl_idx);

}

void CWeapon::activate_physic_shell()
{
	UpdateXForm();
	CPhysicsShellHolder::activate_physic_shell();
}

void CWeapon::setup_physic_shell()
{
	CPhysicsShellHolder::setup_physic_shell();
}

int		g_iWeaponRemove = 1;

bool CWeapon::NeedToDestroyObject()	const
{
	if (GameID() == eGameIDSingle) return false;
	if (Remote()) return false;
	if (H_Parent()) return false;
	if (g_iWeaponRemove == -1) return false;
	if (g_iWeaponRemove == 0) return true;
	if (TimePassedAfterIndependant() > m_dwWeaponRemoveTime)
		return true;

	return false;
}

ALife::_TIME_ID	 CWeapon::TimePassedAfterIndependant()	const
{
	if(!H_Parent() && m_dwWeaponIndependencyTime != 0)
		return Level().timeServer() - m_dwWeaponIndependencyTime;
	else
		return 0;
}

bool CWeapon::can_kill	() const
{
	if (GetSuitableAmmoTotal(true) || m_ammoTypes.empty())
		return				(true);

	return					(false);
}

CInventoryItem *CWeapon::can_kill	(CInventory *inventory) const
{
	if (GetAmmoElapsed() || m_ammoTypes.empty())
		return				(const_cast<CWeapon*>(this));

	TIItemContainer::iterator I = inventory->m_all.begin();
	TIItemContainer::iterator E = inventory->m_all.end();
	for ( ; I != E; ++I) {
		CInventoryItem	*inventory_item = smart_cast<CInventoryItem*>(*I);
		if (!inventory_item)
			continue;
		
		xr_vector<shared_str>::const_iterator	i = std::find(m_ammoTypes.begin(),m_ammoTypes.end(),inventory_item->object().cNameSect());
		if (i != m_ammoTypes.end())
			return			(inventory_item);
	}

	return					(0);
}

const CInventoryItem *CWeapon::can_kill	(const xr_vector<const CGameObject*> &items) const
{
	if (m_ammoTypes.empty())
		return				(this);

	xr_vector<const CGameObject*>::const_iterator I = items.begin();
	xr_vector<const CGameObject*>::const_iterator E = items.end();
	for ( ; I != E; ++I) {
		const CInventoryItem	*inventory_item = smart_cast<const CInventoryItem*>(*I);
		if (!inventory_item)
			continue;

		xr_vector<shared_str>::const_iterator	i = std::find(m_ammoTypes.begin(),m_ammoTypes.end(),inventory_item->object().cNameSect());
		if (i != m_ammoTypes.end())
			return			(inventory_item);
	}

	return					(0);
}

bool CWeapon::ready_to_kill	() const
{
	return					(
		!IsMisfire() && 
		((GetState() == eIdle) || (GetState() == eFire) || (GetState() == eFire2)) && 
		GetAmmoElapsed()
	);
}

void CWeapon::UpdateHudAdditonal(Fmatrix& trans)
{
	CActor* pActor = smart_cast<CActor*>(H_Parent());
	if(!pActor)
		return;

	Fvector summary_offset{}, summary_rotate{};
	const u32 iMovingState = pActor->MovingState();

	u8 idx = GetCurrentHudOffsetIdx();
	const bool b_aiming = idx == 1;

	const auto hi = HudItemData();
	R_ASSERT(hi);
	Fvector zr_offs = hi->m_measures.m_hands_offset[0][idx], zr_rot = hi->m_measures.m_hands_offset[1][idx];
	Fvector curr_offs{}, curr_rot{};
	if(b_aiming)
	{
//		if(idx==0)					return;

		if(pActor->IsZoomAimingMode())
			m_zoom_params.m_fZoomRotationFactor += Device.fTimeDelta/m_zoom_params.m_fZoomRotateTime;
		else
			m_zoom_params.m_fZoomRotationFactor -= Device.fTimeDelta/m_zoom_params.m_fZoomRotateTime;

        clamp(m_zoom_params.m_fZoomRotationFactor, 0.f, 1.f);

        zr_offs.mul(m_zoom_params.m_fZoomRotationFactor);
        zr_rot.mul(m_zoom_params.m_fZoomRotationFactor);

        summary_offset.add(zr_offs);
	}
	
JUMP_EFFECT:
	//=============== Эффекты прыжка ===============//
	{
		const float fJumpPerUpd = Device.fTimeDelta / fJumpMaxTime; // Величина изменение фактора смещения худа при прыжке
		const float fFallPerUpd = Device.fTimeDelta / fFallMaxTime; // Величина изменение фактора смещения худа при падении
		const float fLandingPerUpd = Device.fTimeDelta / fLandingMaxTime; // Величина изменение фактора смещения худа при приземлении (стадия 1)
		const float fLanding2PerUpd = Device.fTimeDelta / fLanding2MaxTime; // Величина изменение фактора смещения худа при приземлении (стадия 2)

		if (iMovingState & mcJump)
		{ // Прыжок
			m_fJump_MovingFactor += fJumpPerUpd;
			m_fFall_MovingFactor -= fJumpPerUpd;
			m_fLanding_MovingFactor -= fJumpPerUpd;
			m_fLanding2_MovingFactor -= fJumpPerUpd;

			clamp(m_fFall_MovingFactor, 0.0f, 1.0f);
			clamp(m_fLanding_MovingFactor, 0.0f, 1.0f);
			clamp(m_fLanding2_MovingFactor, 0.0f, 1.0f);
		}
		else if (iMovingState & mcFall)
		{ // Падание
			m_fJump_MovingFactor -= fFallPerUpd;
			m_fFall_MovingFactor += fFallPerUpd;
			m_fLanding_MovingFactor -= fFallPerUpd;
			m_fLanding2_MovingFactor -= fFallPerUpd;

			clamp(m_fJump_MovingFactor, 0.0f, 1.0f);
			clamp(m_fLanding_MovingFactor, 0.0f, 1.0f);
			clamp(m_fLanding2_MovingFactor, 0.0f, 1.0f);
		}
		else if (iMovingState & mcLanding)
		{ // Приземление
			m_fJump_MovingFactor -= fLandingPerUpd;
			m_fFall_MovingFactor -= fLandingPerUpd;
			m_fLanding_MovingFactor += fLandingPerUpd;
			m_fLanding2_MovingFactor -= fLandingPerUpd;

			clamp(m_fJump_MovingFactor, 0.0f, 1.0f);
			clamp(m_fFall_MovingFactor, 0.0f, 1.0f);
			clamp(m_fLanding2_MovingFactor, 0.0f, 1.0f);
		}
		else if (iMovingState & mcLanding2)
		{ // Приземление
			m_fJump_MovingFactor -= fLanding2PerUpd;
			m_fFall_MovingFactor -= fLanding2PerUpd;
			m_fLanding_MovingFactor -= fLanding2PerUpd;
			m_fLanding2_MovingFactor += fLanding2PerUpd;

			clamp(m_fJump_MovingFactor, 0.0f, 1.0f);
			clamp(m_fFall_MovingFactor, 0.0f, 1.0f);
			clamp(m_fLanding_MovingFactor, 0.0f, 1.0f);
		}
		else
		{ // Двигаемся в любом другом направлении
			if (m_fJump_MovingFactor < 0.0f) // для прыжка
			{
				m_fJump_MovingFactor += fJumpPerUpd;
				clamp(m_fJump_MovingFactor, -1.0f, 0.0f);
			}
			else
			{
				m_fJump_MovingFactor -= fJumpPerUpd;
				clamp(m_fJump_MovingFactor, 0.0f, 1.0f);
			}
			
			if (m_fFall_MovingFactor < 0.0f) // для падения
			{
				m_fFall_MovingFactor += fFallPerUpd;
				clamp(m_fFall_MovingFactor, -1.0f, 0.0f);
			}
			else
			{
				m_fFall_MovingFactor -= fFallPerUpd;
				clamp(m_fFall_MovingFactor, 0.0f, 1.0f);
			}
			
			if (m_fLanding_MovingFactor < 0.0f) // для приземления (стадия 1)
			{
				m_fLanding_MovingFactor += fLandingPerUpd;
				clamp(m_fLanding_MovingFactor, -1.0f, 0.0f);
			}
			else
			{
				m_fLanding_MovingFactor -= fLandingPerUpd;
				clamp(m_fLanding_MovingFactor, 0.0f, 1.0f);
			}
			
			if (m_fLanding2_MovingFactor < 0.0f) // для приземления (стадия 2)
			{
				m_fLanding2_MovingFactor += fLanding2PerUpd;
				clamp(m_fLanding2_MovingFactor, -1.0f, 0.0f);
			}
			else
			{
				m_fLanding2_MovingFactor -= fLanding2PerUpd;
				clamp(m_fLanding2_MovingFactor, 0.0f, 1.0f);
			}
		}

		// не должен превышать эти лимиты
		clamp(m_fJump_MovingFactor, -1.0f, 1.0f);
		clamp(m_fFall_MovingFactor, -1.0f, 1.0f);
		clamp(m_fLanding_MovingFactor, -1.0f, 1.0f);
		clamp(m_fLanding2_MovingFactor, -1.0f, 1.0f);

		// Смещение позиции худа в прыжке
		Fvector jump_offs = m_jump_offset[0]; //pos
		jump_offs.mul(m_fJump_MovingFactor); // Умножаем на фактор эффекта
		// Поворот худа в прыжке
		Fvector jump_rot = m_jump_offset[1]; //rot
		jump_rot.mul(-PI / 180.f); // Преобразуем углы в радианы
		jump_rot.mul(m_fJump_MovingFactor); // Умножаем на фактор эффекта

		// Смещение позиции худа в падении
		Fvector fall_offs = m_fall_offset[0]; //pos
		fall_offs.mul(m_fFall_MovingFactor); // Умножаем на фактор эффекта
		// Поворот худа в падении
		Fvector fall_rot = m_fall_offset[1]; //rot
		fall_rot.mul(-PI / 180.f); // Преобразуем углы в радианы
		fall_rot.mul(m_fFall_MovingFactor); // Умножаем на фактор эффекта

		// Смещение позиции худа в приземлении (стадия 1)
		Fvector landing_offs = m_landing_offset[0]; //pos
		landing_offs.mul(m_fLanding_MovingFactor); // Умножаем на фактор эффекта
		// Поворот худа в приземлении (стадия 1)
		Fvector landing_rot = m_landing_offset[1]; //rot
		landing_rot.mul(-PI / 180.f); // Преобразуем углы в радианы
		landing_rot.mul(m_fLanding_MovingFactor); // Умножаем на фактор эффекта

		// Смещение позиции худа в приземлении (стадия 2)
		Fvector landing2_offs = m_landing2_offset[0]; //pos
		landing2_offs.mul(m_fLanding2_MovingFactor); // Умножаем на фактор эффекта
		// Поворот худа в приземлении (стадия 2)
		Fvector landing2_rot = m_landing2_offset[1]; //rot
		landing2_rot.mul(-PI / 180.f); // Преобразуем углы в радианы
		landing2_rot.mul(m_fLanding2_MovingFactor); // Умножаем на фактор эффекта

		// Применяем
		summary_offset.add(jump_offs);
		summary_rotate.add(jump_rot);

		summary_offset.add(fall_offs);
		summary_rotate.add(fall_rot);

		summary_offset.add(landing_offs);
		summary_rotate.add(landing_rot);

		summary_offset.add(landing2_offs);
		summary_rotate.add(landing2_rot);
	}
	//====================================================//

APPLY_EFFECTS:
	//================ Применение эффектов ===============//
	{
		// поворот с сохранением смещения by Zander
		Fvector _angle{}, _pos{ trans.c };
		trans.getHPB(_angle);
		_angle.add(Fvector{-summary_rotate.x, -summary_rotate.y, -summary_rotate.z});
		//Msg("##[%s] summary_rotate: [%f,%f,%f]", __FUNCTION__, summary_rotate.x, summary_rotate.y, summary_rotate.z);
		trans.setHPB(_angle.x, _angle.y, _angle.z);
		trans.c = _pos;

		Fmatrix hud_rotation;
		hud_rotation.identity();

		if (b_aiming)
		{
			hud_rotation.rotateX(zr_rot.x);

			Fmatrix hud_rotation_y;
			hud_rotation_y.identity();
			hud_rotation_y.rotateY(zr_rot.y);
			hud_rotation.mulA_43(hud_rotation_y);

			hud_rotation_y.identity();
			hud_rotation_y.rotateZ(zr_rot.z);
			hud_rotation.mulA_43(hud_rotation_y);
			//Msg("~~[%s] zr_rot: [%f,%f,%f]", __FUNCTION__, zr_rot.x, zr_rot.y, zr_rot.z);
		}
		//Msg("--[%s] summary_offset: [%f,%f,%f]", __FUNCTION__, summary_offset.x, summary_offset.y, summary_offset.z);
		hud_rotation.translate_over(summary_offset);
		trans.mulB_43(hud_rotation);
	}
	//====================================================//
}

void CWeapon::SetAmmoElapsed(int ammo_count)
{
	iAmmoElapsed				= ammo_count;

	u32 uAmmo					= u32(iAmmoElapsed);

	if (uAmmo != m_magazine.size())
	{
		if (uAmmo > m_magazine.size())
		{
			CCartridge			l_cartridge; 
			l_cartridge.Load	(m_ammoTypes[m_ammoType].c_str(), m_ammoType);
			while (uAmmo > m_magazine.size())
				m_magazine.push_back(l_cartridge);
		}
		else
		{
			while (uAmmo < m_magazine.size())
				m_magazine.pop_back();
		};
	};
}

u32	CWeapon::ef_main_weapon_type	() const
{
	VERIFY	(m_ef_main_weapon_type != u32(-1));
	return	(m_ef_main_weapon_type);
}

u32	CWeapon::ef_weapon_type	() const
{
	VERIFY	(m_ef_weapon_type != u32(-1));
	return	(m_ef_weapon_type);
}

bool CWeapon::IsNecessaryItem	    (const shared_str& item_sect)
{
	return (std::find(m_ammoTypes.begin(), m_ammoTypes.end(), item_sect) != m_ammoTypes.end() );
}

void CWeapon::modify_holder_params		(float &range, float &fov) const
{
	if (!IsScopeAttached()) {
		inherited::modify_holder_params	(range,fov);
		return;
	}
	range	*= m_addon_holder_range_modifier;
	fov		*= m_addon_holder_fov_modifier;
}

bool CWeapon::render_item_ui_query()
{
	bool b_is_active_item = (m_pInventory->ActiveItem()==this);
	bool res = b_is_active_item && IsZoomed() && ZoomHideCrosshair() && ZoomTexture() && !IsRotatingToZoom();
	return res;
}

void CWeapon::render_item_ui()
{
	if(m_zoom_params.m_pVision)
		m_zoom_params.m_pVision->Draw();

	ZoomTexture()->Update	();
	ZoomTexture()->Draw		();
}

bool CWeapon::unlimited_ammo() 
{ 
	//if (IsGameTypeSingle())
	{
		if(m_pInventory)
		{
			return inventory_owner().unlimited_ammo() && m_DefaultCartridge.m_flags.test(CCartridge::cfCanBeUnlimited);
		}
		else
			return false;
	}

	//return ((GameID() == eGameIDDeathmatch) && m_DefaultCartridge.m_flags.test(CCartridge::cfCanBeUnlimited)); 
			
};

float CWeapon::Weight() const
{
	float res = CInventoryItemObject::Weight();
	if(IsGrenadeLauncherAttached()&&GetGrenadeLauncherName().size()){
		res += pSettings->r_float(GetGrenadeLauncherName(),"inv_weight");
	}
	if(IsScopeAttached()&&m_scopes.size()){
		res += pSettings->r_float(GetScopeName(),"inv_weight");
	}
	if(IsSilencerAttached()&&GetSilencerName().size()){
		res += pSettings->r_float(GetSilencerName(),"inv_weight");
	}
	
	if(iAmmoElapsed)
	{
		float w		= pSettings->r_float(m_ammoTypes[m_ammoType].c_str(),"inv_weight");
		float bs	= pSettings->r_float(m_ammoTypes[m_ammoType].c_str(),"box_size");

		res			+= w*(iAmmoElapsed/bs);
	}

	return res;
}

bool CWeapon::show_crosshair()
{
	return !IsPending() && ( !IsZoomed() || (!ZoomHideCrosshair() || !GetHUDmode()) );
}

bool CWeapon::show_indicators()
{
	return ! ( IsZoomed() && ZoomTexture() );
}

float CWeapon::GetConditionToShow	() const
{
	return	(GetCondition());//powf(GetCondition(),4.0f));
}

BOOL CWeapon::ParentMayHaveAimBullet	()
{
	CObject* O=H_Parent();
	CEntityAlive* EA=smart_cast<CEntityAlive*>(O);
	return EA->cast_actor()!=0;
}

BOOL CWeapon::ParentIsActor	()
{
	CObject* O			= H_Parent();
	if (!O)
		return FALSE;

	CEntityAlive* EA	= smart_cast<CEntityAlive*>(O);
	if (!EA)
		return FALSE;

	return EA->cast_actor()!=0;
}

extern u32 hud_adj_mode;

void CWeapon::debug_draw_firedeps()
{
#ifdef DEBUG
	if(hud_adj_mode==5||hud_adj_mode==6||hud_adj_mode==7)
	{
		CDebugRenderer			&render = Level().debug_renderer();

		if(hud_adj_mode==5)
			render.draw_aabb(get_LastFP(),	0.005f,0.005f,0.005f,D3DCOLOR_XRGB(255,0,0));

		if(hud_adj_mode==6)
			render.draw_aabb(get_LastFP2(),	0.005f,0.005f,0.005f,D3DCOLOR_XRGB(0,0,255));

		if(hud_adj_mode==7)
			render.draw_aabb(get_LastSP(),		0.005f,0.005f,0.005f,D3DCOLOR_XRGB(0,255,0));
	}
#endif // DEBUG
}

const float &CWeapon::hit_probability	() const
{
	VERIFY					((g_SingleGameDifficulty >= egdNovice) && (g_SingleGameDifficulty <= egdMaster)); 
	return					(m_hit_probability[egdNovice]);
}

void CWeapon::OnStateSwitch	(u32 S)
{
	inherited::OnStateSwitch(S);
	m_BriefInfo_CalcFrame = 0;

	//if(GetState()==eReload)
	//{
	//	if(H_Parent()==Level().CurrentEntity() && !fsimilar(m_zoom_params.m_ReloadDof.w,-1.0f))
	//	{
	//		CActor* current_actor	= smart_cast<CActor*>(H_Parent());
	//		if (current_actor)
	//			current_actor->Cameras().AddCamEffector(xr_new<CEffectorDOF>(m_zoom_params.m_ReloadDof) );
	//	}
	//}
}

void CWeapon::OnAnimationEnd(u32 state) 
{
	inherited::OnAnimationEnd(state);
}

u8 CWeapon::GetCurrentHudOffsetIdx()
{
	if(const auto wpn_w_gl = smart_cast<CWeaponMagazinedWGrenade*>(this))
	{
		if(wpn_w_gl->m_bGrenadeMode)
			return 2;
	}

	const bool b_aiming = ((IsZoomed() && m_zoom_params.m_fZoomRotationFactor <= 1.f) || (!IsZoomed() && m_zoom_params.m_fZoomRotationFactor > 0.f));
	return	b_aiming;
}

void CWeapon::render_hud_mode()
{
	RenderLight();
}

bool CWeapon::MovingAnimAllowedNow()
{
	return !IsZoomed();
}

bool CWeapon::IsHudModeNow()
{
	return (HudItemData()!=NULL);
}

void CWeapon::ZoomInc()
{
	if(!IsScopeAttached())					return;
	if(!m_zoom_params.m_bUseDynamicZoom)	return;
	float delta,min_zoom_factor;
	GetZoomData(m_zoom_params.m_fScopeZoomFactor, delta, min_zoom_factor);

	float f					= GetZoomFactor()-delta;
	clamp					(f,m_zoom_params.m_fScopeZoomFactor,min_zoom_factor);
	SetZoomFactor			( f );
}

void CWeapon::ZoomDec()
{
	if(!IsScopeAttached())					return;
	if(!m_zoom_params.m_bUseDynamicZoom)	return;
	float delta,min_zoom_factor;
	GetZoomData(m_zoom_params.m_fScopeZoomFactor,delta,min_zoom_factor);

	float f					= GetZoomFactor()+delta;
	clamp					(f,m_zoom_params.m_fScopeZoomFactor,min_zoom_factor);
	SetZoomFactor			( f );

}
u32 CWeapon::Cost() const
{
	u32 res = CInventoryItem::Cost();
	if(IsGrenadeLauncherAttached()&&GetGrenadeLauncherName().size()){
		res += pSettings->r_u32(GetGrenadeLauncherName(),"cost");
	}
	if(IsScopeAttached()&&m_scopes.size()){
		res += pSettings->r_u32(GetScopeName(),"cost");
	}
	if(IsSilencerAttached()&&GetSilencerName().size()){
		res += pSettings->r_u32(GetSilencerName(),"cost");
	}
	
	if(iAmmoElapsed)
	{
		float w		= pSettings->r_float(m_ammoTypes[m_ammoType].c_str(),"cost");
		float bs	= pSettings->r_float(m_ammoTypes[m_ammoType].c_str(),"box_size");

		res			+= iFloor(w*(iAmmoElapsed/bs));
	}
	return res;

}

int CWeapon::GetScopeX()
{
	if (bUseAltScope)
	{
		if (m_eScopeStatus != ALife::eAddonPermanent && IsScopeAttached())
		{
			return pSettings->r_s32(GetNameWithAttachment(), "scope_x");
		}
		else
		{
			return 0;
		}
	}
	else
		return pSettings->r_s32(m_scopes[m_cur_scope], "scope_x");
}

int	CWeapon::GetScopeY()
{
	if (bUseAltScope)
	{
		if (m_eScopeStatus != ALife::eAddonPermanent && IsScopeAttached())
		{
			return pSettings->r_s32(GetNameWithAttachment(), "scope_y");
		}
		else
		{
			return 0;
		}
	}
	else
		return pSettings->r_s32(m_scopes[m_cur_scope], "scope_y");
}

bool CWeapon::bLoadAltScopesParams(LPCSTR section)
{
	 if (!pSettings->line_exist(section, "scopes"))
		return false;

	if (!xr_strcmp(pSettings->r_string(section, "scopes"), "none"))
		return false;

	//Msg("Load AltScopes [%s]", section);

	if (m_eScopeStatus == ALife::eAddonAttachable)
	{
		LPCSTR str = pSettings->r_string(section, "scopes");
		for (int i = 0, count = _GetItemCount(str); i < count; ++i)
		{
			string128 scope_section;
			_GetItem(str, i, scope_section);
			m_scopes.push_back(scope_section);
			//Msg("Scope [%s]", scope_section);
		}
	}
	else if (m_eScopeStatus == ALife::eAddonPermanent)
	{
		LoadCurrentScopeParams(section);
	}

	return true;
}

void CWeapon::LoadOriginalScopesParams(LPCSTR section)
{
	//Msg("Load Original [%s]", section);

	if (m_eScopeStatus == ALife::eAddonAttachable)
	{
		if (pSettings->line_exist(section, "scopes_sect"))
		{
			LPCSTR str = pSettings->r_string(section, "scopes_sect");
			for (int i = 0, count = _GetItemCount(str); i < count; ++i)
			{
				string128						scope_section;
				_GetItem(str, i, scope_section);
				m_scopes.push_back(scope_section);
			}
		}
		else
		{
			m_scopes.push_back(section);
		}
	}
	else if (m_eScopeStatus == ALife::eAddonPermanent)
	{
		LoadCurrentScopeParams(section);
	}
}

void CWeapon::LoadCurrentScopeParams(LPCSTR section)
{
	shared_str scope_tex_name = "none";
	bScopeIsHasTexture = false;

	if (pSettings->line_exist(section, "scope_texture"))
	{
		scope_tex_name = pSettings->r_string(section, "scope_texture");
		if (xr_strcmp(scope_tex_name, "none") != 0)
			bScopeIsHasTexture = true;
	}

	m_zoom_params.m_fScopeZoomFactor = pSettings->r_float(section, "scope_zoom_factor");

	if (m_UIScope)
	{
		xr_delete(m_UIScope);
	}

	if (!g_dedicated_server)
		if (bScopeIsHasTexture)
		{
			m_UIScope = xr_new<CUIWindow>();
			if (!pWpnScopeXml)
			{
				pWpnScopeXml = xr_new<CUIXml>();
				pWpnScopeXml->Load(CONFIG_PATH, UI_PATH, "scopes.xml");
			}
			CUIXmlInit::InitWindow(*pWpnScopeXml, scope_tex_name.c_str(), 0, m_UIScope);
		}
}

shared_str CWeapon::GetNameWithAttachment()
{
	string64 str;

	if (pSettings->line_exist(m_section_id.c_str(), "parent_section"))
	{
		shared_str parent = pSettings->r_string(m_section_id.c_str(), "parent_section");
		xr_sprintf(str, "%s_%s", parent.c_str(), GetScopeName().c_str());
	}
	else
	{
		xr_sprintf(str, "%s_%s", m_section_id.c_str(), GetScopeName().c_str());
	}

	return (shared_str)str;

}

void CWeapon::UpdateAltScope()
{
	if (m_eScopeStatus != ALife::eAddonAttachable || !bUseAltScope)
		return;

	shared_str sectionNeedLoad = IsScopeAttached() ? GetNameWithAttachment() : m_section_id;

	if (!pSettings->section_exist(sectionNeedLoad))
		return;

	shared_str vis = pSettings->r_string(sectionNeedLoad, "visual");

	if (vis != cNameVisual())
	{
		cNameVisual_set(vis);
	}

	shared_str new_hud = pSettings->r_string(sectionNeedLoad, "hud");

	if (new_hud != hud_sect)
	{
		hud_sect = new_hud;
	}
}

const shared_str CWeapon::GetScopeName() const
{
	if (bUseAltScope)
	{
		return m_scopes[m_cur_scope];
	}
	else
	{
		return pSettings->r_string(m_scopes[m_cur_scope], "scope_name");
	}
}

#include "HUDManager.h"

float CWeapon::GetHudFov()
{
	// Рассчитываем HUD FOV от бедра (с учётом упирания в стены)
	if (ParentIsActor() && Level().CurrentViewEntity() == H_Parent())
	{
		// Получаем расстояние от камеры до точки в прицеле
		collide::rq_result& RQ = HUD().GetCurrentRayQuery();
		float dist = RQ.range;

		// Интерполируем расстояние в диапазон от 0 (min) до 1 (max)
		clamp(dist, m_nearwall_dist_min, m_nearwall_dist_max);
		float fDistanceMod =
			((dist - m_nearwall_dist_min) / (m_nearwall_dist_max - m_nearwall_dist_min)); // 0.f ... 1.f

		// Рассчитываем базовый HUD FOV от бедра
		float fBaseFov = psHUD_FOV_def + m_hud_fov_add_mod;
		clamp(fBaseFov, 0.0f, FLT_MAX);

		// Плавно высчитываем итоговый FOV от бедра
		float src = m_nearwall_speed_mod * Device.fTimeDelta;
		clamp(src, 0.f, 1.f);

		float fTrgFov = m_nearwall_target_hud_fov + fDistanceMod * (fBaseFov - m_nearwall_target_hud_fov);
		m_nearwall_last_hud_fov = m_nearwall_last_hud_fov * (1 - src) + fTrgFov * src;
	}

	return m_nearwall_last_hud_fov;
	 
}

void CWeapon::LoadModParams(LPCSTR section)
{
	// Модификатор для HUD FOV от бедра
	m_hud_fov_add_mod = READ_IF_EXISTS(pSettings, r_float, section, "hud_fov_addition_modifier", 0.f);

	// Параметры изменения HUD FOV, когда игрок стоит вплотную к стене
	m_nearwall_dist_min = READ_IF_EXISTS(pSettings, r_float, section, "nearwall_dist_min", 0.5f);
	m_nearwall_dist_max = READ_IF_EXISTS(pSettings, r_float, section, "nearwall_dist_max", 1.f);
	m_nearwall_target_hud_fov = READ_IF_EXISTS(pSettings, r_float, section, "nearwall_target_hud_fov", 0.27f);
	m_nearwall_speed_mod = READ_IF_EXISTS(pSettings, r_float, section, "nearwall_speed_mod", 10.f);
}

bool CWeapon::IsPartlyReloading() 
{
	return iAmmoElapsed > 0;
}