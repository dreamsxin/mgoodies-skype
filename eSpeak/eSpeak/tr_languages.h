/***************************************************************************
 *   Copyright (C) 2005 to 2007 by Jonathan Duddington                     *
 *   email: jonsd@users.sourceforge.net                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write see:                           *
 *               <http://www.gnu.org/licenses/>.                           *
 ***************************************************************************/





class Translator_English: public Translator
{//=======================================

public:
	Translator_English();
	int Unpronouncable(char *word);
};  // end of class Translator_English



class Translator_Russian: public Translator
{//=======================================

public:
	Translator_Russian();
private:
	int ChangePhonemes(PHONEME_LIST2 *phlist, int n_ph, int index, PHONEME_TAB *ph, CHANGEPH *ch);

};  // end of class Translator_Russian





class Translator_Afrikaans: public Translator
{//==========================================

public:
	Translator_Afrikaans();
private:
	int TranslateChar(char *ptr, int prev_in, unsigned int c, unsigned int next_in, int *insert);

};  // end of class Translator_Afrikaans

