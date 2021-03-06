/*
 * Copyright 2010 by Ewan Meadows (sonny_jim@hotmail.com)
 *
 * This file is part of FreeWPC.
 *
 * FreeWPC is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FreeWPC is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FreeWPC; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */


#include <freewpc.h>
#include <gate.h>

CALLSET_ENTRY (leftramp, sw_enter_mpf)
{
	gate_stop ();
	sound_send (MUS_MPF_ENTER);
}

CALLSET_ENTRY (leftramp, sw_mpf_exit_left)
{
	sound_send (SND_MPF_EXIT);
	event_should_follow (mpf_exit, right_inlane, TIME_4S);
}

CALLSET_ENTRY (leftramp, sw_mpf_exit_right)
{
	sound_send (SND_MPF_EXIT);
	flag_on (FLAG_SKILLSHOT_ENABLED);
	event_should_follow (mpf_exit, shooter, TIME_4S);
}
