#pragma once

#pragma warning(disable:4995)
#include "../xrEngine/stdafx.h"
#include "DPlay/dplay8.h"
#pragma warning(default:4995)
#pragma warning( 4 : 4018 )
#pragma warning( 4 : 4244 )
#pragma warning(disable:4505)

#define	THROW VERIFY
#define	THROW2 VERIFY2
#define	THROW3 VERIFY3

#include "../xrEngine/gamefont.h"
#include "../xrEngine/xr_object.h"
#include "../xrEngine/igame_level.h"
#include "../xrphysics/xrphysics.h"
#include "smart_cast.h"