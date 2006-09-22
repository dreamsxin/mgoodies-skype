/* 
Copyright (C) 2006 Ricardo Pescuma Domenecci

This is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with this file; see the file license.txt.  If
not, write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  
*/


#ifndef __MUSIC_H__
# define __MUSIC_H__


#include "commons.h"


// Players
#include "players\\player.h"
#include "players\\watrack.h"
#include "players\\winamp.h"
#include "players\\itunes.h"
#include "players\\wmp.h"


// First non polling ones
#define WATRACK 0
#define WMP 1
#define WINAMP 2
#define ITUNES 3
#define NUM_PLAYERS 4

extern Player *players[];


void InitMusic();
void FreeMusic();
void EnableDisablePlayers();


int ChangedListeningInfo();
BOOL GetListeningInfo(LISTENINGTOINFO *lti);


// Helper functions to players
void FreeListeningInfo(LISTENINGTOINFO *lti);




#endif // __MUSIC_H__