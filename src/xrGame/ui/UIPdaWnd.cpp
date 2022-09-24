#include "stdafx.h"
#include "UIPdaWnd.h"
#include "../Pda.h"

#include "xrUIXmlParser.h"
#include "UIXmlInit.h"
#include "UIInventoryUtilities.h"

#include "../level.h"
#include "UIGameCustom.h"

#include "UIStatic.h"
#include "UIFrameWindow.h"
#include "UITabControl.h"
#include "UIMapWnd.h"
#include "UIFrameLineWnd.h"
#include "object_broker.h"
#include "UIMessagesWindow.h"
#include "UIMainIngameWnd.h"
#include "UITabButton.h"
#include "UIAnimatedStatic.h"
#include "uimessageswindow.h"

#include "UIHelper.h"
#include "UIHint.h"
#include "UIBtnHint.h"
#include "UITaskWnd.h"
#include "UIRankingWnd.h"
#include "UILogsWnd.h"

#include "UIScriptWnd.h"

#include "Actor.h"
#include "Inventory.h"
#include "../xrEngine/XR_IOConsole.h"
#include "ui\UIProgressBar.h"
#include "player_hud.h"

#define PDA_XML		"pda.xml"

u32 g_pda_info_state = 0;

void RearrangeTabButtons(CUITabControl* pTab);

CUIPdaWnd::CUIPdaWnd()
{
	pUITaskWnd       = NULL;
//-	pUIFactionWarWnd = NULL;
	pUIRankingWnd    = NULL;
	pUILogsWnd       = NULL;
	m_pMessagesWnd       = nullptr;
	m_hint_wnd       = NULL;
	last_cursor_pos.set(UI_BASE_WIDTH / 2.f, UI_BASE_HEIGHT / 2.f);
	m_cursor_box.set(0.f, 0.f, UI_BASE_WIDTH, UI_BASE_HEIGHT);
	Init();
}

CUIPdaWnd::~CUIPdaWnd()
{
	delete_data( pUITaskWnd );
//-	delete_data( pUIFactionWarWnd );
	delete_data( pUIRankingWnd );
	delete_data( pUILogsWnd );
	delete_data( m_hint_wnd );
	delete_data(m_pMessagesWnd);
	delete_data( UINoice );
}

void CUIPdaWnd::Init()
{
	CUIXml					uiXml;
	uiXml.Load				(CONFIG_PATH, UI_PATH, PDA_XML);

	m_pActiveDialog			= NULL;
	m_sActiveSection		= "";

	CUIXmlInit::InitWindow	(uiXml, "main", 0, this);

	UIMainPdaFrame			= UIHelper::CreateStatic	( uiXml, "background_static", this );
	m_caption				= UIHelper::CreateTextWnd	( uiXml, "caption_static", this );
	m_caption_const			= ( m_caption->GetText() );
	m_clock					= UIHelper::CreateTextWnd	( uiXml, "clock_wnd", this );
/*
	m_anim_static			= xr_new<CUIAnimatedStatic>();
	AttachChild				(m_anim_static);
	m_anim_static->SetAutoDelete(true);
	CUIXmlInit::InitAnimatedStatic(uiXml, "anim_static", 0, m_anim_static);
*/
	m_btn_close				= UIHelper::Create3tButton( uiXml, "close_button", this );
	m_hint_wnd				= UIHelper::CreateHint( uiXml, "hint_wnd" );


	pUITaskWnd					= xr_new<CUITaskWnd>();
	pUITaskWnd->hint_wnd		= m_hint_wnd;
	pUITaskWnd->Init			();

	// pUIFactionWarWnd				= xr_new<CUIFactionWarWnd>();
	// pUIFactionWarWnd->hint_wnd		= m_hint_wnd;
	// pUIFactionWarWnd->Init			();

	pUIRankingWnd					= xr_new<CUIRankingWnd>();
	pUIRankingWnd->Init				();

	pUILogsWnd						= xr_new<CUILogsWnd>();
	pUILogsWnd->Init				();

	m_pMessagesWnd						= xr_new<CUIMessagesWindow>();

	UITabControl					= xr_new<CUITabControl>();
	UITabControl->SetAutoDelete		(true);
	AttachChild						(UITabControl);
	CUIXmlInit::InitTabControl		(uiXml, "tab", 0, UITabControl);
	UITabControl->SetMessageTarget	(this);

	UINoice					= xr_new<CUIStatic>();
	UINoice->SetAutoDelete	( true );
	CUIXmlInit::InitStatic	( uiXml, "noice_static", 0, UINoice );

//	RearrangeTabButtons		(UITabControl);
}

void CUIPdaWnd::SendMessage(CUIWindow* pWnd, s16 msg, void* pData)
{
	switch ( msg )
	{
	case TAB_CHANGED:
		{
			if ( pWnd == UITabControl )
			{
				SetActiveSubdialog			(UITabControl->GetActiveId());
			}
			break;
		}
	case BUTTON_CLICKED:
		{
			if ( pWnd == m_btn_close )
			{
				HideDialog();
			}
			break;
		}
	default:
		{
			if (m_pActiveDialog)
				m_pActiveDialog->SendMessage	(pWnd, msg, pData);
		}
	};
}

enum TCursorDirection
{
	Idle, Up, Down, Left, Right, UpLeft, DownLeft, DownRight, UpRight
};

float GetAngleByLegs(const float x, const float y)
{
	const float gyp = sqrt(x * x + y * y);
	float k = y / gyp;
	clamp(k, -1.f, 1.f);

	constexpr float pi = 3.1415926535;

	float result = std::asin(k);
	if (x < 0)
		result = pi - result;
	if (result < 0)
		result += 2 * pi;

	return result;
};

TCursorDirection GetPDADirByAngle(const float x, const float y)
{
	TCursorDirection result = Idle;
	const float angle = GetAngleByLegs(x, y);

	if ((angle>=0.393) && (angle<1.18))
		result = DownRight;
	else if ((angle>=1.18) && (angle<1.96))
		result = Down;
	else if ((angle>=1.96) && (angle<2.74))
		result = DownLeft;
	else if ((angle>=2.74) && (angle<3.53))
		result = Left;
	else if ((angle>=3.53) && (angle<4.32))
		result = UpLeft;
	else if ((angle>=4.32) && (angle<5.10))
		result = Up;
	else if ((angle>=5.10) && (angle<5.89))
		result = UpRight;
	else
		result = Right;

	return result;
};

const char* GetPDAJoystickAnimationModifier(const float x, const float y)
{
	const char* result = nullptr;

	switch(GetPDADirByAngle(x, y))
	{
		case Up: result = "_up"; break;
		case UpRight: result = "_up_right"; break;
		case Right: result = "_right"; break;
		case DownRight: result = "_down_right"; break;
		case Down: result = "_down"; break;
		case DownLeft: result = "_down_left"; break;
		case Left: result = "_left"; break;
		case UpLeft: result = "_up_left"; break;
		default: result = ""; break;
	}

	return result;
};

bool CUIPdaWnd::OnMouseAction(float x, float y, EUIMessages mouse_action)
{
	switch (mouse_action)
	{
	case WINDOW_LBUTTON_DOWN:
	case WINDOW_RBUTTON_DOWN:
	case WINDOW_LBUTTON_UP:
	case WINDOW_RBUTTON_UP:
	{
		CPda* pda = Actor()->GetPDA();
		if (pda)
		{
			if (pda->IsPending())
				return true;

			if (mouse_action == WINDOW_LBUTTON_DOWN)
				bButtonL = true;
			else if (mouse_action == WINDOW_RBUTTON_DOWN)
				bButtonR = true;
			else if (mouse_action == WINDOW_LBUTTON_UP)
				bButtonL = false;
			else if (mouse_action == WINDOW_RBUTTON_UP)
				bButtonR = false;
		}
		break;
	}
	}
	CUIDialogWnd::OnMouseAction(x, y, mouse_action);
	return true; //always true because StopAnyMove() == false
}

static const char* prev_prefix{};
static u32 TIME{}, last_click_time{};
void CUIPdaWnd::MouseMovement(float x, float y)
{
	if(TIME > Device.dwTimeGlobal)
		return;

	CPda* pda = Actor()->GetPDA();
	if (!pda || pda->IsPending()) return;

	if(!_abs(x) && !_abs(y))
	{
		if(m_dwLastClickTime != last_click_time && std::string(pda->anm_prefix) == "" && (pda->GetCurrentMotion() == "anm_idle" || pda->GetCurrentMotion() == "anm_idle_moving"))
			pda->anm_prefix = "_click";
		else
			pda->anm_prefix = "";
	}
	else
		pda->anm_prefix = GetPDAJoystickAnimationModifier(x, y);

	if(prev_prefix && prev_prefix == pda->anm_prefix)
		return;
	
	prev_prefix = pda->anm_prefix;
	pda->PlayAnimIdle();

	last_click_time = m_dwLastClickTime;
	TIME = Device.dwTimeGlobal + 200;
}

void CUIPdaWnd::Show(bool status)
{
	inherited::Show						(status);
	if(status)
	{
		InventoryUtilities::SendInfoToActor	("ui_pda");
		
		if (m_sActiveSection == NULL || strcmp(m_sActiveSection.c_str(), "") == 0)
		{
			SetActiveSubdialog("eptTasks");
			UITabControl->SetActiveTab("eptTasks");
		}
		else
			SetActiveSubdialog(m_sActiveSection);

		CurrentGameUI()->HideActorMenu();
	}
	else
	{
		InventoryUtilities::SendInfoToActor	("ui_pda_hide");
		CurrentGameUI()->UIMainIngameWnd->SetFlashIconState_(CUIMainIngameWnd::efiPdaTask, false);
		if (m_pActiveDialog)
		{
			m_pActiveDialog->Show(false);
			m_pActiveDialog = pUITaskWnd; //hack for script window
		}
		g_btnHint->Discard();
		g_statHint->Discard();
	}
}

void CUIPdaWnd::Update()
{
	inherited::Update();
	if (m_pActiveDialog)
		m_pActiveDialog->Update();
	m_clock->TextItemControl().SetText(
		InventoryUtilities::GetGameTimeAsString(InventoryUtilities::etpTimeToMinutes).c_str());

	pUILogsWnd->PerformWork();
}

#include "string_table.h"
#include "UIChatWnd.h"
void CUIPdaWnd::SetActiveSubdialog(const shared_str& section)
{
	if ( m_pActiveDialog )
	{
		if (UIMainPdaFrame->IsChild(m_pActiveDialog))
			UIMainPdaFrame->DetachChild(m_pActiveDialog);
		m_pActiveDialog->Show(false);
	}

	if ( section == "eptTasks" )
	{
		m_pActiveDialog = pUITaskWnd;
	}
//-	else if ( section == "eptFractionWar" )
//-	{
//-		m_pActiveDialog = pUIFactionWarWnd;
//-	}
	else if ( section == "eptRanking" )
	{
		m_pActiveDialog = pUIRankingWnd;
	}
	else if ( section == "eptLogs" )
	{
		m_pActiveDialog = pUILogsWnd;
	}
	else if ( section == "eptChat" )
	{
		string512					prefix;
		CStringTable st;
		xr_sprintf(prefix, "%s> ", st.translate("st_mp_say_to_all").c_str());
		
		auto pChatWnd = m_pMessagesWnd->GetChatWnd();
		pChatWnd->ChatToAll			(true);
		pChatWnd->SetEditBoxPrefix	(prefix);
		
		m_pActiveDialog = m_pMessagesWnd;
	}

	R_ASSERT2						(m_pActiveDialog, "active dialog is not initialized");

	if (!UIMainPdaFrame->IsChild(m_pActiveDialog))
		UIMainPdaFrame->AttachChild(m_pActiveDialog);
	m_pActiveDialog->Show			(true);

	if (m_pActiveDialog)
	{
		if (!UIMainPdaFrame->IsChild(m_pActiveDialog))
			UIMainPdaFrame->AttachChild(m_pActiveDialog);
		m_pActiveDialog->Show(true);
		m_sActiveSection = section;
		SetActiveCaption();
	}
	else
	{
		m_sActiveSection = "";
	}
}

void CUIPdaWnd::SetActiveCaption()
{
	TABS_VECTOR*	btn_vec		= UITabControl->GetButtonsVector();
	TABS_VECTOR::iterator it_b	= btn_vec->begin();
	TABS_VECTOR::iterator it_e	= btn_vec->end();
	for ( ; it_b != it_e; ++it_b )
	{
		if ( (*it_b)->m_btn_id == m_sActiveSection )
		{
			LPCSTR cur = (*it_b)->TextItemControl()->GetText();
			string256 buf;
			strconcat(sizeof(buf), buf, m_caption_const.c_str(), cur);
			SetCaption(buf);
			UITabControl->Show(true);
			m_caption->Show(true);
			return;
		}
	}

	UITabControl->Show(false);
	m_caption->Show(false);
}

#include "UICursor.h"
void CUIPdaWnd::ResetCursor()
{
	if (!last_cursor_pos.similar({ 0.f, 0.f }))
		GetUICursor().SetUICursorPosition(last_cursor_pos);
}

void CUIPdaWnd::Show_SecondTaskWnd( bool status )
{
	if ( status )
	{
		SetActiveSubdialog( "eptTasks" );
	}
	pUITaskWnd->Show_TaskListWnd( status );
}

void CUIPdaWnd::Show_MapLegendWnd( bool status )
{
	if ( status )
	{
		SetActiveSubdialog( "eptTasks" );
	}
	pUITaskWnd->ShowMapLegend( status );
}

void CUIPdaWnd::Draw()
{
	if (Device.dwFrame == dwPDAFrame)
		return;

	dwPDAFrame = Device.dwFrame;
	
	inherited::Draw();
//.	DrawUpdatedSections();
	DrawHint();
	UINoice->Draw(); // over all
}

void CUIPdaWnd::DrawHint()
{
	if (m_sActiveSection == "eptTasks")
	{
		pUITaskWnd->DrawHint();
	}
//-	else if ( m_pActiveDialog == pUIFactionWarWnd )
//-	{
//		m_hint_wnd->Draw();
//-	}
	else if (m_sActiveSection == "eptRanking")
	{
		pUIRankingWnd->DrawHint();
	}
	else if (m_sActiveSection == "eptLogs")
	{

	}
	m_hint_wnd->Draw();
}

void CUIPdaWnd::UpdatePda()
{
	pUILogsWnd->UpdateNews();

	if (m_sActiveSection == "eptTasks")
	{
		pUITaskWnd->ReloadTaskInfo();
	}
}

void CUIPdaWnd::UpdateRankingWnd()
{
	pUIRankingWnd->Update();
}

void CUIPdaWnd::Reset()
{
	inherited::ResetAll		();

	if ( pUITaskWnd )		pUITaskWnd->ResetAll();
//-	if ( pUIFactionWarWnd )	pUITaskWnd->ResetAll();
	if ( pUIRankingWnd )	pUIRankingWnd->ResetAll();
	if ( pUILogsWnd )		pUILogsWnd->ResetAll();
	if (m_pMessagesWnd)		m_pMessagesWnd->ResetAll();
}

void CUIPdaWnd::SetCaption( LPCSTR text )
{
	m_caption->SetText( text );
}

void RearrangeTabButtons(CUITabControl* pTab)
{
	TABS_VECTOR *	btn_vec		= pTab->GetButtonsVector();
	TABS_VECTOR::iterator it	= btn_vec->begin();
	TABS_VECTOR::iterator it_e	= btn_vec->end();

	Fvector2					pos;
	pos.set						((*it)->GetWndPos());
	float						size_x;

	for ( ; it != it_e; ++it )
	{
		(*it)->SetWndPos		(pos);
		(*it)->AdjustWidthToText();
		size_x					= (*it)->GetWndSize().x + 30.0f;
		(*it)->SetWidth			(size_x);
		pos.x					+= size_x - 6.0f;
	}
	
	pTab->SetWidth( pos.x + 5.0f );
	pos.x = pTab->GetWndPos().x - pos.x;
	pos.y = pTab->GetWndPos().y;
	pTab->SetWndPos( pos );
}

void CUIPdaWnd::Enable(bool status)
{
	if (status)
		ResetCursor();
	else
	{
		g_player_hud->reset_thumb(false);
		ResetJoystick(false);
		bButtonL = false;
		bButtonR = false;
	}

	inherited::Enable(status);
}

bool CUIPdaWnd::OnKeyboardAction(int dik, EUIMessages keyboard_action)
{
	if (WINDOW_KEY_PRESSED == keyboard_action && IsShown())
	{
		if (!psActorFlags.test(AF_3D_PDA))
		{
			EGameActions action = get_binded_action(dik);

			if (action == kQUIT || action == kINVENTORY || action == kACTIVE_JOBS)
			{
				HideDialog();
				return true;
			}
			return inherited::OnKeyboardAction(dik, keyboard_action);
		}

		const auto pActor = smart_cast<CActor*>(Level().CurrentEntity());
		if (!pActor)
			return inherited::OnKeyboardAction(dik, keyboard_action);

		CPda* pda = pActor->GetPDA();
		if (pda)
		{
			EGameActions action = get_binded_action(dik);

			if (action == kQUIT) // "Hack" to make Esc key open main menu instead of simply hiding the PDA UI
			{
				if (pda->GetState() == CPda::eHiding || pda->GetState() == CPda::eHidden)
				{
					HideDialog();
					Console->Execute("main_menu");
				}
				else
					pActor->inventory().Activate(NO_ACTIVE_SLOT);

				return true;
			}

			if (action == kUSE || action == kACTIVE_JOBS || action == kINVENTORY || (action > kCAM_ZOOM_OUT && action < kWPN_NEXT)) // Since UI no longer passes non-movement inputs to the actor input receiver this is needed now.
			{
				CObject* obj = (GameID() == eGameIDSingle) ? Level().CurrentEntity() : Level().CurrentControlEntity();
				{
					IInputReceiver* IR = smart_cast<IInputReceiver*>(smart_cast<CGameObject*>(obj));
					if (IR) IR->IR_OnKeyboardPress(action);
				}
				return true;
			}

			// Don't allow zoom in while draw/holster animation plays, freelook is enabled or a hand animation plays
			if (pda->IsPending())
				return false;

			if (action == kWPN_ZOOM)
			{
				if (!pda->m_bZoomed && !IsEnabled())
				{
					pActor->StopSprint();

					// Input state change must be deferred because actor state can still be sprinting when activating which would instantly deactivate input again
					pda->m_eDeferredEnable = CPda::eDeferredEnableState::eEnableZoomed;
				}
				pda->m_bZoomed = !pda->m_bZoomed;
				return true;
			}

			// Чё за кал вообще
			/*if (action == kWPN_FUNC || (!IsEnabled() && action == kWPN_FIRE))
			{
				if (IsEnabled())
				{
					pda->m_bZoomed = false;
					Enable(false);
				}
				else
				{
					Actor()->StopSprint();

					// Input state change must be deferred because actor state can still be sprinting when activating which would instantly deactivate input again
					pda->m_eDeferredEnable = CPda::eDeferredEnableState::eEnable;
				}
				return true;
			}*/
		}
	}
	return inherited::OnKeyboardAction(dik, keyboard_action);
}