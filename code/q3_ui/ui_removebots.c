/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
//
/*
=======================================================================

REMOVE BOTS MENU

=======================================================================
*/


#include "ui_local.h"


#define ART_BACKGROUND		"menu/art/addbotframe"
#define ART_BACK0			"menu/art/back_0"
#define ART_BACK1			"menu/art/back_1"	
#define ART_DELETE0			"menu/art/delete_0"
#define ART_DELETE1			"menu/art/delete_1"
#define ART_ARROWS			"menu/art/arrows_vert_0"
#define ART_ARROWUP			"menu/art/arrows_vert_top"
#define ART_ARROWDOWN		"menu/art/arrows_vert_bot"

#define ID_UP				10
#define ID_DOWN				11
#define ID_DELETE			12
#define ID_BACK				13
#define ID_BOTNAME0			20
#define ID_BOTNAME1			21
#define ID_BOTNAME2			22
#define ID_BOTNAME3			23
#define ID_BOTNAME4			24
#define ID_BOTNAME5			25
#define ID_BOTNAME6			26

#define MAX_RECENT_REMOVALS	16
#define REMOVAL_TRACK_MS	8000	// track removals for 8 seconds

typedef struct {
	menuframework_s	menu;

	menutext_s		banner;
	menubitmap_s	background;

	menubitmap_s	arrows;
	menubitmap_s	up;
	menubitmap_s	down;

	menutext_s		bots[7];

	menubitmap_s	delete;
	menubitmap_s	back;
	menutext_s      status;

	int				numBots;
	int				baseBotNum;
	int				selectedBotNum;
	char			botnames[7][32];
	int				botClientNums[MAX_BOTS];
	char            statusText[64];
} removeBotsMenuInfo_t;

static removeBotsMenuInfo_t	removeBotsMenuInfo;

typedef struct {
	int clientNum;
	int removalTime;
} recentRemoval_t;

static recentRemoval_t	recentRemovals[MAX_RECENT_REMOVALS];
static int				numRecentRemovals = 0;


/*
=================
UI_RemoveBotsMenu_TrackRemoval
=================
*/
static void UI_RemoveBotsMenu_TrackRemoval( int clientNum ) {
	int i;

	// Update existing entry if present
	for( i = 0; i < numRecentRemovals; i++ ) {
		if( recentRemovals[i].clientNum == clientNum ) {
			recentRemovals[i].removalTime = uis.realtime;
			return;
		}
	}

	// Add new entry
	if( numRecentRemovals < MAX_RECENT_REMOVALS ) {
		recentRemovals[numRecentRemovals].clientNum = clientNum;
		recentRemovals[numRecentRemovals].removalTime = uis.realtime;
		numRecentRemovals++;
	}
}


/*
=================
UI_RemoveBotsMenu_IsRecentlyRemoved
=================
*/
static qboolean UI_RemoveBotsMenu_IsRecentlyRemoved( int clientNum ) {
	int i;

	for( i = 0; i < numRecentRemovals; i++ ) {
		if( recentRemovals[i].clientNum == clientNum ) {
			if( uis.realtime - recentRemovals[i].removalTime < REMOVAL_TRACK_MS ) {
				return qtrue;
			}
		}
	}
	return qfalse;
}


/*
=================
UI_RemoveBotsMenu_CleanupRemovals
=================
*/
static void UI_RemoveBotsMenu_CleanupRemovals( void ) {
	int i, j;

	for( i = 0; i < numRecentRemovals; ) {
		if( uis.realtime - recentRemovals[i].removalTime >= REMOVAL_TRACK_MS ) {
			// Shift down to fill gap
			for( j = i; j < numRecentRemovals - 1; j++ ) {
				recentRemovals[j] = recentRemovals[j + 1];
			}
			numRecentRemovals--;
		} else {
			i++;
		}
	}
}


/*
=================
UI_RemoveBotsMenu_SetBotNames
=================
*/
static void UI_RemoveBotsMenu_SetBotNames( void ) {
	int		n;
	char	info[MAX_INFO_STRING];

	// Clear all slots first to prevent ghost entries
	for ( n = 0; n < 7; n++ ) {
		removeBotsMenuInfo.botnames[n][0] = '\0';
	}

	for ( n = 0; (n < 7) && (removeBotsMenuInfo.baseBotNum + n < removeBotsMenuInfo.numBots); n++ ) {
		trap_GetConfigString( CS_PLAYERS + removeBotsMenuInfo.botClientNums[removeBotsMenuInfo.baseBotNum + n], info, MAX_INFO_STRING );
		Q_strncpyz( removeBotsMenuInfo.botnames[n], Info_ValueForKey( info, "n" ), sizeof(removeBotsMenuInfo.botnames[n]) );
		Q_CleanStr( removeBotsMenuInfo.botnames[n] );
	}
}


/*
=================
UI_RemoveBotsMenu_GetBots
=================
*/
static void UI_RemoveBotsMenu_GetBots( void ) {
	int		numPlayers;
	int		isBot;
	int		n;
	char	info[MAX_INFO_STRING];

	trap_GetConfigString( CS_SERVERINFO, info, sizeof(info) );
	numPlayers = atoi( Info_ValueForKey( info, "sv_maxclients" ) );
	removeBotsMenuInfo.numBots = 0;

	for( n = 0; n < numPlayers; n++ ) {
		trap_GetConfigString( CS_PLAYERS + n, info, MAX_INFO_STRING );

		isBot = atoi( Info_ValueForKey( info, "skill" ) );
		if( !isBot ) {
			continue;
		}

		// Skip bots we recently removed. In Q3, the UI menu blocks the game
		// loop so clientkick commands are queued but not processed until we
		// exit the menus. The CS_PLAYERS config strings remain stale until
		// a snapshot updates them after we resume gameplay. We track our
		// removals locally to hide this lag from the user.
		if( UI_RemoveBotsMenu_IsRecentlyRemoved( n ) ) {
			continue;
		}

		removeBotsMenuInfo.botClientNums[removeBotsMenuInfo.numBots] = n;
		removeBotsMenuInfo.numBots++;
	}
}


/*
=================
UI_RemoveBotsMenu_UpdateDisplay
=================
*/
static void UI_RemoveBotsMenu_UpdateDisplay( void ) {
	int     count;
	int     n;

	// Clamp baseBotNum if we removed the last page
	if( removeBotsMenuInfo.baseBotNum >= removeBotsMenuInfo.numBots ) {
		if( removeBotsMenuInfo.numBots > 0 ) {
			removeBotsMenuInfo.baseBotNum = removeBotsMenuInfo.numBots - 1;
			// Align to page boundary (7 items per page)
			removeBotsMenuInfo.baseBotNum = (removeBotsMenuInfo.baseBotNum / 7) * 7;
		} else {
			removeBotsMenuInfo.baseBotNum = 0;
		}
	}

	// Clamp selection
	count = removeBotsMenuInfo.numBots - removeBotsMenuInfo.baseBotNum;
	if( count > 7 ) {
		count = 7;
	}
	if( removeBotsMenuInfo.selectedBotNum >= count ) {
		removeBotsMenuInfo.selectedBotNum = 0;
	}
	if( removeBotsMenuInfo.numBots == 0 ) {
		removeBotsMenuInfo.selectedBotNum = 0;
	}

	// Update displayed names
	UI_RemoveBotsMenu_SetBotNames();

	// Update selection highlight
	if( count > 0 ) {
		for( n = 0; n < 7; n++ ) {
			removeBotsMenuInfo.bots[n].color = color_orange;
		}
		removeBotsMenuInfo.bots[removeBotsMenuInfo.selectedBotNum].color = color_white;
	}
}


/*
=================
UI_RemoveBotsMenu_RefreshList
=================
*/
static void UI_RemoveBotsMenu_RefreshList( void ) {
	// Re-scan for current bots from server config strings
	UI_RemoveBotsMenu_GetBots();
	UI_RemoveBotsMenu_UpdateDisplay();
}


/*
=================
UI_RemoveBotsMenu_DeleteEvent
=================
*/
static void UI_RemoveBotsMenu_DeleteEvent( void* ptr, int event ) {
	int botIndex;
	const char *botName;
	char buf[128];
	int i;

	if (event != QM_ACTIVATED) {
		return;
	}

	botIndex = removeBotsMenuInfo.baseBotNum + removeBotsMenuInfo.selectedBotNum;

	// Bounds check
	if( botIndex >= removeBotsMenuInfo.numBots ) {
		return;
	}

	botName = removeBotsMenuInfo.botnames[removeBotsMenuInfo.selectedBotNum];

	// Immediate console print via UI trap
	Com_sprintf(buf, sizeof(buf), "^1Removing bot:^7 %s\n", botName);
	trap_Print(buf);

	// Status message in menu. The bot will actually be kicked when we exit
	// the UI menus and the game loop resumes processing commands.
	Com_sprintf(removeBotsMenuInfo.statusText, sizeof(removeBotsMenuInfo.statusText), 
		"Removing: %s", botName);
	removeBotsMenuInfo.status.generic.flags &= ~QMF_HIDDEN;

	// Queue the clientkick command. Note: Q3 UI menus block the game loop,
	// so this command won't be processed by the server until we exit the
	// menus and return to gameplay.
	trap_Cmd_ExecuteText( EXEC_APPEND, va("clientkick %i\n", 
		removeBotsMenuInfo.botClientNums[botIndex]) );

	// Track this removal so we filter stale config strings on re-entry.
	// The CS_PLAYERS config string won't update until a snapshot arrives
	// after we resume gameplay.
	UI_RemoveBotsMenu_TrackRemoval( removeBotsMenuInfo.botClientNums[botIndex] );

	// IMMEDIATELY remove from our local list so the UI updates right away.
	// The server-side kick is deferred, but the user sees instant feedback.
	for( i = botIndex; i < removeBotsMenuInfo.numBots - 1; i++ ) {
		removeBotsMenuInfo.botClientNums[i] = removeBotsMenuInfo.botClientNums[i + 1];
	}
	removeBotsMenuInfo.numBots--;

	// Refresh the display immediately using our updated local list
	UI_RemoveBotsMenu_UpdateDisplay();
}


/*
=================
UI_RemoveBotsMenu_BotEvent
=================
*/
static void UI_RemoveBotsMenu_BotEvent( void* ptr, int event ) {
	if (event != QM_ACTIVATED) {
		return;
	}

	removeBotsMenuInfo.bots[removeBotsMenuInfo.selectedBotNum].color = color_orange;
	removeBotsMenuInfo.selectedBotNum = ((menucommon_s*)ptr)->id - ID_BOTNAME0;
	removeBotsMenuInfo.bots[removeBotsMenuInfo.selectedBotNum].color = color_white;
}


/*
=================
UI_RemoveBotsMenu_BackEvent
=================
*/
static void UI_RemoveBotsMenu_BackEvent( void* ptr, int event ) {
	if (event != QM_ACTIVATED) {
		return;
	}
	UI_PopMenu();
}


/*
=================
UI_RemoveBotsMenu_UpEvent
=================
*/
static void UI_RemoveBotsMenu_UpEvent( void* ptr, int event ) {
	if (event != QM_ACTIVATED) {
		return;
	}

	if( removeBotsMenuInfo.baseBotNum > 0 ) {
		removeBotsMenuInfo.baseBotNum--;
		UI_RemoveBotsMenu_SetBotNames();
		removeBotsMenuInfo.status.generic.flags |= QMF_HIDDEN;
	}
}


/*
=================
UI_RemoveBotsMenu_DownEvent
=================
*/
static void UI_RemoveBotsMenu_DownEvent( void* ptr, int event ) {
	if (event != QM_ACTIVATED) {
		return;
	}

	if( removeBotsMenuInfo.baseBotNum + 7 < removeBotsMenuInfo.numBots ) {
		removeBotsMenuInfo.baseBotNum++;
		UI_RemoveBotsMenu_SetBotNames();
		removeBotsMenuInfo.status.generic.flags |= QMF_HIDDEN;
	}
}


/*
=================
UI_RemoveBots_Cache
=================
*/
void UI_RemoveBots_Cache( void ) {
	trap_R_RegisterShaderNoMip( ART_BACKGROUND );
	trap_R_RegisterShaderNoMip( ART_BACK0 );
	trap_R_RegisterShaderNoMip( ART_BACK1 );
	trap_R_RegisterShaderNoMip( ART_DELETE0 );
	trap_R_RegisterShaderNoMip( ART_DELETE1 );
}


/*
=================
UI_RemoveBotsMenu_Init
=================
*/
static void UI_RemoveBotsMenu_Init( void ) {
	int		n;
	int		count;
	int		y;

	memset( &removeBotsMenuInfo, 0 ,sizeof(removeBotsMenuInfo) );
	removeBotsMenuInfo.menu.fullscreen = qfalse;
	removeBotsMenuInfo.menu.wrapAround = qtrue;

	UI_RemoveBots_Cache();

	// Clean up old tracked removals before scanning. This is necessary
	// because Q3 UI menus block the game loop, so clientkick commands
	// are deferred until we exit menus. CS_PLAYERS config strings remain
	// stale until snapshots update after gameplay resumes.
	UI_RemoveBotsMenu_CleanupRemovals();
	UI_RemoveBotsMenu_GetBots();
	UI_RemoveBotsMenu_SetBotNames();
	count = removeBotsMenuInfo.numBots < 7 ? removeBotsMenuInfo.numBots : 7;

	removeBotsMenuInfo.banner.generic.type		= MTYPE_BTEXT;
	removeBotsMenuInfo.banner.generic.x			= 320;
	removeBotsMenuInfo.banner.generic.y			= 16;
	removeBotsMenuInfo.banner.string			= "REMOVE BOTS";
	removeBotsMenuInfo.banner.color				= color_white;
	removeBotsMenuInfo.banner.style				= UI_CENTER;

	removeBotsMenuInfo.background.generic.type	= MTYPE_BITMAP;
	removeBotsMenuInfo.background.generic.name	= ART_BACKGROUND;
	removeBotsMenuInfo.background.generic.flags	= QMF_INACTIVE;
	removeBotsMenuInfo.background.generic.x		= 320-233;
	removeBotsMenuInfo.background.generic.y		= 240-166;
	removeBotsMenuInfo.background.width			= 466;
	removeBotsMenuInfo.background.height		= 332;

	removeBotsMenuInfo.arrows.generic.type		= MTYPE_BITMAP;
	removeBotsMenuInfo.arrows.generic.name		= ART_ARROWS;
	removeBotsMenuInfo.arrows.generic.flags		= QMF_INACTIVE;
	removeBotsMenuInfo.arrows.generic.x			= 200;
	removeBotsMenuInfo.arrows.generic.y			= 128;
	removeBotsMenuInfo.arrows.width				= 64;
	removeBotsMenuInfo.arrows.height			= 128;

	removeBotsMenuInfo.up.generic.type			= MTYPE_BITMAP;
	removeBotsMenuInfo.up.generic.flags			= QMF_LEFT_JUSTIFY|QMF_PULSEIFFOCUS;
	removeBotsMenuInfo.up.generic.x				= 200;
	removeBotsMenuInfo.up.generic.y				= 128;
	removeBotsMenuInfo.up.generic.id			= ID_UP;
	removeBotsMenuInfo.up.generic.callback		= UI_RemoveBotsMenu_UpEvent;
	removeBotsMenuInfo.up.width					= 64;
	removeBotsMenuInfo.up.height				= 64;
	removeBotsMenuInfo.up.focuspic				= ART_ARROWUP;

	removeBotsMenuInfo.down.generic.type		= MTYPE_BITMAP;
	removeBotsMenuInfo.down.generic.flags		= QMF_LEFT_JUSTIFY|QMF_PULSEIFFOCUS;
	removeBotsMenuInfo.down.generic.x			= 200;
	removeBotsMenuInfo.down.generic.y			= 128+64;
	removeBotsMenuInfo.down.generic.id			= ID_DOWN;
	removeBotsMenuInfo.down.generic.callback	= UI_RemoveBotsMenu_DownEvent;
	removeBotsMenuInfo.down.width				= 64;
	removeBotsMenuInfo.down.height				= 64;
	removeBotsMenuInfo.down.focuspic			= ART_ARROWDOWN;

	for( n = 0, y = 120; n < count; n++, y += 20 ) {
		removeBotsMenuInfo.bots[n].generic.type		= MTYPE_PTEXT;
		removeBotsMenuInfo.bots[n].generic.flags	= QMF_LEFT_JUSTIFY|QMF_PULSEIFFOCUS;
		removeBotsMenuInfo.bots[n].generic.id		= ID_BOTNAME0 + n;
		removeBotsMenuInfo.bots[n].generic.x		= 320 - 56;
		removeBotsMenuInfo.bots[n].generic.y		= y;
		removeBotsMenuInfo.bots[n].generic.callback	= UI_RemoveBotsMenu_BotEvent;
		removeBotsMenuInfo.bots[n].string			= removeBotsMenuInfo.botnames[n];
		removeBotsMenuInfo.bots[n].color			= color_orange;
		removeBotsMenuInfo.bots[n].style			= UI_LEFT|UI_SMALLFONT;
	}

	removeBotsMenuInfo.delete.generic.type		= MTYPE_BITMAP;
	removeBotsMenuInfo.delete.generic.name		= ART_DELETE0;
	removeBotsMenuInfo.delete.generic.flags		= QMF_LEFT_JUSTIFY|QMF_PULSEIFFOCUS;
	removeBotsMenuInfo.delete.generic.id		= ID_DELETE;
	removeBotsMenuInfo.delete.generic.callback	= UI_RemoveBotsMenu_DeleteEvent;
	removeBotsMenuInfo.delete.generic.x			= 320+128-128;
	removeBotsMenuInfo.delete.generic.y			= 256+128-64;
	removeBotsMenuInfo.delete.width  			= 128;
	removeBotsMenuInfo.delete.height  			= 64;
	removeBotsMenuInfo.delete.focuspic			= ART_DELETE1;

	removeBotsMenuInfo.back.generic.type		= MTYPE_BITMAP;
	removeBotsMenuInfo.back.generic.name		= ART_BACK0;
	removeBotsMenuInfo.back.generic.flags		= QMF_LEFT_JUSTIFY|QMF_PULSEIFFOCUS;
	removeBotsMenuInfo.back.generic.id			= ID_BACK;
	removeBotsMenuInfo.back.generic.callback	= UI_RemoveBotsMenu_BackEvent;
	removeBotsMenuInfo.back.generic.x			= 320-128;
	removeBotsMenuInfo.back.generic.y			= 256+128-64;
	removeBotsMenuInfo.back.width				= 128;
	removeBotsMenuInfo.back.height				= 64;
	removeBotsMenuInfo.back.focuspic			= ART_BACK1;

	// Status line at bottom of menu
	removeBotsMenuInfo.status.generic.type     = MTYPE_PTEXT;
	removeBotsMenuInfo.status.generic.flags    = QMF_INACTIVE | QMF_HIDDEN;
	removeBotsMenuInfo.status.generic.x        = -104;
	removeBotsMenuInfo.status.generic.y        = 0;
	removeBotsMenuInfo.status.string           = removeBotsMenuInfo.statusText;
	removeBotsMenuInfo.status.color            = color_yellow;
	removeBotsMenuInfo.status.style            = UI_LEFT|UI_SMALLFONT;
	removeBotsMenuInfo.statusText[0] = '\0';

	Menu_AddItem( &removeBotsMenuInfo.menu, &removeBotsMenuInfo.background );
	Menu_AddItem( &removeBotsMenuInfo.menu, &removeBotsMenuInfo.banner );
	Menu_AddItem( &removeBotsMenuInfo.menu, &removeBotsMenuInfo.arrows );
	Menu_AddItem( &removeBotsMenuInfo.menu, &removeBotsMenuInfo.up );
	Menu_AddItem( &removeBotsMenuInfo.menu, &removeBotsMenuInfo.down );
	for( n = 0; n < count; n++ ) {
		Menu_AddItem( &removeBotsMenuInfo.menu, &removeBotsMenuInfo.bots[n] );
	}
	Menu_AddItem( &removeBotsMenuInfo.menu, &removeBotsMenuInfo.delete );
	Menu_AddItem( &removeBotsMenuInfo.menu, &removeBotsMenuInfo.back );
	Menu_AddItem( &removeBotsMenuInfo.menu, &removeBotsMenuInfo.status );

	removeBotsMenuInfo.baseBotNum = 0;
	removeBotsMenuInfo.selectedBotNum = 0;
	removeBotsMenuInfo.bots[0].color = color_white;
}


/*
=================
UI_RemoveBotsMenu
=================
*/
void UI_RemoveBotsMenu( void ) {
	UI_RemoveBotsMenu_Init();
	UI_PushMenu( &removeBotsMenuInfo.menu );
}
