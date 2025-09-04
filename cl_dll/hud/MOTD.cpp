/***
*
*	Copyright (c) 1999, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
//
// MOTD.cpp
//
// for displaying a server-sent message of the day
//

#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "kbutton.h"
#include "triangleapi.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "draw_util.h"
#include "build.h"

#if XASH_WIN32 == 1 || XASH_PSVITA == 1
#define strcasestr strstr
#endif

// HTML cache directory and file constants
#define HTMLCACHE_DIR "cstrike"
#define MOTD_CACHE_FILE "cached.html"
#define MAX_CACHE_PATH 256

int CHudMOTD :: Init( void )
{
	gHUD.AddHudElem( this );

	HOOK_MESSAGE( gHUD.m_MOTD, MOTD );

	cl_hide_motd = CVAR_CREATE("cl_hide_motd", "0", FCVAR_ARCHIVE); // hide motd
	HOOK_COMMAND( gHUD.m_MOTD, "_motd_open_browser", OpenMOTDInBrowser );
	HOOK_COMMAND( gHUD.m_MOTD, "_motd_close", CloseMOTDDialog );
	Reset();

	return 1;
}

int CHudMOTD :: VidInit( void )
{
	// Load sprites here
	return 1;
}

void CHudMOTD :: Reset( void )
{
	m_iFlags &= ~HUD_DRAW;  // start out inactive
	m_szMOTD[0] = 0;
	m_iLines = 0;
	m_bShow = false;
	ignoreThisMotd = false;
	memset(m_szMOTD, 0, sizeof(m_szMOTD));
	memset(m_szCachedFilePath, 0, sizeof(m_szCachedFilePath));
}

// Cache MOTD content to HTML file
int CHudMOTD :: CacheMOTDToHTML()
{
	// Generate cache file path
	snprintf(m_szCachedFilePath, sizeof(m_szCachedFilePath), "%s/%s", HTMLCACHE_DIR, MOTD_CACHE_FILE);	

	// Write m_szMOTD as-is (already HTML)
	FILE *f = fopen(m_szCachedFilePath, "w");
	if (f)
	{
		fwrite(m_szMOTD, 1, strlen(m_szMOTD), f);
		fclose(f);
		return 1;
	}
	return 0;
}

// Launch external browser with cached MOTD
void CHudMOTD :: LaunchExternalBrowser()
{
	if (!m_szCachedFilePath[0])
		return;

	char fullPath[512];
#if defined(_WIN32)
	_fullpath(fullPath, m_szCachedFilePath, sizeof(fullPath));
	char cmd[600];
	snprintf(cmd, sizeof(cmd), "start \"\" \"%s\"", fullPath);
#elif defined(__APPLE__)
	char *resolved = realpath(m_szCachedFilePath, NULL);
	const char *pathForCmd = resolved ? resolved : m_szCachedFilePath;
	char cmd[600];
	snprintf(cmd, sizeof(cmd), "open \"%s\"", pathForCmd);
	if (resolved) free(resolved);
#elif defined(__ANDROID__)
	char *resolved = realpath(m_szCachedFilePath, NULL);
	const char *pathForCmd = resolved ? resolved : m_szCachedFilePath;
	char cmd[700];
	// Use Android Activity Manager to open in a browser
	snprintf(cmd, sizeof(cmd), "/system/bin/am start -a android.intent.action.VIEW -d \"file://%s\" -t \"text/html\"", pathForCmd);
	if (resolved) free(resolved);
#else
	char *resolved = realpath(m_szCachedFilePath, NULL);
	const char *pathForCmd = resolved ? resolved : m_szCachedFilePath;
	char cmd[600];
	snprintf(cmd, sizeof(cmd), "xdg-open \"%s\"", pathForCmd);
	if (resolved) free(resolved);
#endif

	system(cmd);
}

// Add command handler functions - these must match the CHudUserCmd declarations
void CHudMOTD :: UserCmd_OpenMOTDInBrowser()
{
	LaunchExternalBrowser();
	Reset();
}

void CHudMOTD :: UserCmd_CloseMOTDDialog()
{
	Reset();
}

// Modify Draw function to handle mouse clicks
int CHudMOTD :: Draw( float fTime )
{
	if ( !m_bShow )
		return 1;

	int dialogWidth = 400;
	int dialogHeight = 200;
	int dialogX = (ScreenWidth - dialogWidth) / 2;
	int dialogY = (ScreenHeight - dialogHeight) / 2;

	DrawUtils::DrawRectangle(dialogX, dialogY, dialogWidth, dialogHeight, 0, 0, 0, 200);
	DrawUtils::DrawRectangle(dialogX, dialogY, dialogWidth, 2, 255, 255, 255, 255); // top
	DrawUtils::DrawRectangle(dialogX, dialogY + dialogHeight - 2, dialogWidth, 2, 255, 255, 255, 255); // bottom
	DrawUtils::DrawRectangle(dialogX, dialogY, 2, dialogHeight, 255, 255, 255, 255); // left
	DrawUtils::DrawRectangle(dialogX + dialogWidth - 2, dialogY, 2, dialogHeight, 255, 255, 255, 255); // right

	const char* title = "MOTD - External Browser";
	const char* line1 = "Press O to open MOTD in your default browser.";
	const char* line2 = "Press ESC or C to close this dialog.";

	int titleWidth = DrawUtils::ConsoleStringLen(title);
	int line1Width = DrawUtils::ConsoleStringLen(line1);
	int line2Width = DrawUtils::ConsoleStringLen(line2);

	int centerX = dialogX + (dialogWidth - titleWidth) / 2;
	int centerLine1X = dialogX + (dialogWidth - line1Width) / 2;
	int centerLine2X = dialogX + (dialogWidth - line2Width) / 2;

	DrawUtils::DrawConsoleString(centerX, dialogY + 20, title);
	DrawUtils::DrawConsoleString(centerLine1X, dialogY + 60, line1);
	DrawUtils::DrawConsoleString(centerLine2X, dialogY + 90, line2);

	return 1;
}


int CHudMOTD :: MsgFunc_MOTD( const char *pszName, int iSize, void *pbuf )
{
	if( cl_hide_motd->value )
		return 1;

	if ( m_iFlags & HUD_DRAW )
	{
		Reset(); // clear the current MOTD in prep for this one
	}

	if( ignoreThisMotd )
		return 1;

	BufferReader reader( pszName, pbuf, iSize );

	int is_finished = reader.ReadByte();
	strcat( m_szMOTD, reader.ReadString() );

	if ( is_finished )
	{	
		// Cache MOTD to HTML file
		if (CacheMOTDToHTML())
		{
			m_iFlags |= HUD_DRAW;
			m_bShow = true;
		}
	}

	return 1;
}
