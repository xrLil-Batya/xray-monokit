#include "stdafx.h"
#include "weaponpistol.h"
#include "ParticlesObject.h"
#include "actor.h"

CWeaponPistol::CWeaponPistol()
{
	m_eSoundClose		= ESoundTypes(SOUND_TYPE_WEAPON_RECHARGING);
	SetPending			(FALSE);
}

CWeaponPistol::~CWeaponPistol(void)
{
}

void CWeaponPistol::net_Destroy()
{
	inherited::net_Destroy();
}


void CWeaponPistol::Load	(LPCSTR section)
{
	inherited::Load		(section);

	m_sounds.LoadSound(section, "snd_close", "sndClose", false, m_eSoundClose);
}

void CWeaponPistol::OnH_B_Chield		()
{
	inherited::OnH_B_Chield		();
}

void CWeaponPistol::PlayAnimShow	()
{
	VERIFY(GetState()==eShowing);

	if(iAmmoElapsed==0)
		PlayHUDMotion("anm_show_empty", FALSE, this, GetState());
	else
		inherited::PlayAnimShow();
}

void CWeaponPistol::PlayAnimBore()
{
	if(iAmmoElapsed==0)
		PlayHUDMotion	("anm_bore_empty", TRUE, this, GetState());
	else
		inherited::PlayAnimBore();
}

void CWeaponPistol::PlayAnimIdleSprint()
{
	if(iAmmoElapsed==0)
	{
		PlayHUDMotion("anm_idle_sprint_empty", TRUE, NULL, GetState());
	}else{
		inherited::PlayAnimIdleSprint();
	}
}

void CWeaponPistol::PlayAnimIdleMoving()
{
	if(iAmmoElapsed==0)
	{
		PlayHUDMotion("anm_idle_moving_empty", TRUE, NULL, GetState());
	}else{
		inherited::PlayAnimIdleMoving();
	}
}

void CWeaponPistol::PlayAnimIdleMovingSlow()
{
	if (!iAmmoElapsed)
		PlayHUDMotionIfExists({ "anm_idle_moving_slow_empty", "anim_empty", "anm_idle_moving_empty" }, true, GetState());
	else
		inherited::PlayAnimIdleMovingSlow();
}

void CWeaponPistol::PlayAnimIdleMovingCrouch()
{
	if (!iAmmoElapsed)
		PlayHUDMotionIfExists({ "anm_idle_moving_crouch_empty", "anim_empty", "anm_idle_moving_empty" }, true, GetState());
	else
		inherited::PlayAnimIdleMovingCrouch();
}

void CWeaponPistol::PlayAnimIdleMovingCrouchSlow()
{
	if (!iAmmoElapsed)
		PlayHUDMotionIfExists({ "anm_idle_moving_crouch_slow_empty", "anm_idle_moving_crouch_empty", "anim_empty", "anm_idle_moving_empty" }, true, GetState());
	else
		inherited::PlayAnimIdleMovingCrouchSlow();
}

void CWeaponPistol::PlayAnimIdle()
{
	if (TryPlayAnimIdle()) return;

	if (IsZoomed())
		PlayAnimAim();
	else if (!iAmmoElapsed)
	{
		if (IsRotatingFromZoom()) {
			if (isHUDAnimationExist("anm_idle_aim_end_empty")) {
				PlayHUDMotionNew("anm_idle_aim_end_empty", true, GetState());
				return;
			}
		}

		PlayHUDMotionIfExists({ "anim_empty", "anm_idle_empty" }, true, GetState());
	}
	else
		inherited::PlayAnimIdle		();
}

void CWeaponPistol::PlayAnimAim()
{
	if (!iAmmoElapsed)
	{
		if (IsRotatingToZoom()) {
			if (isHUDAnimationExist("anm_idle_aim_start_empty")) {
				PlayHUDMotionNew("anm_idle_aim_start_empty", true, GetState());
				return;
			}
		}

		if (const char* guns_aim_anm = GetAnimAimName()) {
			string64 guns_aim_anm_full;
			strconcat(sizeof(string64), guns_aim_anm_full, guns_aim_anm, "_empty");
			if (isHUDAnimationExist(guns_aim_anm_full)) {
				PlayHUDMotionNew(guns_aim_anm_full, true, GetState());
				return;
			}
		}

		PlayHUDMotionIfExists({ "anim_empty", "anm_idle_aim_empty" }, true, GetState());
	}
	else
		inherited::PlayAnimAim();
}

void CWeaponPistol::PlayAnimReload()
{	
	VERIFY(GetState()==eReload);

	if(IsMisfire() && isHUDAnimationExist("anm_reload_jammed"))
		PlayHUDMotion("anm_reload_jammed", TRUE, this, GetState());
	else if(!IsPartlyReloading())
		PlayHUDMotion("anm_reload_empty", TRUE, this, GetState());
	else
		PlayHUDMotion("anm_reload", TRUE, this, GetState());
}


void CWeaponPistol::PlayAnimHide()
{
	VERIFY(GetState()==eHiding);
	if(iAmmoElapsed==0) 
	{
		PlaySound			("sndClose", get_LastFP());
		PlayHUDMotion		("anm_hide_empty" , TRUE, this, GetState());
	} 
	else 
		inherited::PlayAnimHide();
}

void CWeaponPistol::PlayAnimShoot	()
{
	VERIFY(GetState()==eFire);
	string_path guns_shoot_anm{};
	strconcat(sizeof(string_path), guns_shoot_anm, "anm_shoot", (this->IsZoomed() && !this->IsRotatingToZoom()) ? "_aim" : "", iAmmoElapsed == 1 ? "_last" : "", this->IsSilencerAttached() ? "_sil" : "");
	if (isHUDAnimationExist(guns_shoot_anm)) {
		PlayHUDMotionNew(guns_shoot_anm, false, GetState());
		return;
	}
	
	if(iAmmoElapsed > 1) 
	{
		PlayHUDMotion("anm_shots" , FALSE, this, GetState());
	}
	else 
	{
		PlayHUDMotion("anm_shot_l", FALSE, this, GetState()); 
	}
}


void CWeaponPistol::switch2_Reload()
{
	inherited::switch2_Reload();
}

void CWeaponPistol::OnAnimationEnd(u32 state)
{
	inherited::OnAnimationEnd(state);
}

void CWeaponPistol::OnShot		()
{
	PlaySound		(m_sSndShotCurrent.c_str(),get_LastFP());

	AddShotEffector	();
	
	PlayAnimShoot	();

	// Shell Drop
	Fvector vel; 
	PHGetLinearVell(vel);
	OnShellDrop					(get_LastSP(),  vel);

	// ќгонь из ствола
	
	StartFlameParticles	();
	R_ASSERT2(!m_pFlameParticles || !m_pFlameParticles->IsLooped(),
			  "can't set looped particles system for shoting with pistol");
	
	//дым из ствола
	StartSmokeParticles	(get_LastFP(), vel);
}

void CWeaponPistol::UpdateSounds()
{
	inherited::UpdateSounds();
	m_sounds.SetPosition("sndClose", get_LastFP());
}