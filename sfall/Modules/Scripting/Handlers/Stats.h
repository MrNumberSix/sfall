/*
 *    sfall
 *    Copyright (C) 2008-2016  The sfall team
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

namespace sfall
{
namespace script
{

// stat_funcs
void sf_set_pc_base_stat(OpcodeContext&);

void sf_set_pc_extra_stat(OpcodeContext&);

void sf_get_pc_base_stat(OpcodeContext&);

void sf_get_pc_extra_stat(OpcodeContext&);

void sf_set_critter_base_stat(OpcodeContext&);

void sf_set_critter_extra_stat(OpcodeContext&);

void sf_get_critter_base_stat(OpcodeContext&);

void sf_get_critter_extra_stat(OpcodeContext&);

void __declspec() op_set_critter_skill_points();

void __declspec() op_get_critter_skill_points();

void __declspec() op_set_available_skill_points();

void __declspec() op_get_available_skill_points();

void __declspec() op_mod_skill_points_per_level();

void __declspec() op_get_critter_current_ap();

void __declspec() op_set_critter_current_ap();

void __declspec() op_set_pickpocket_max();

void __declspec() op_set_hit_chance_max();

void __declspec() op_set_critter_hit_chance_mod();

void __declspec() op_set_base_hit_chance_mod();

void __declspec() op_set_critter_pickpocket_mod();

void __declspec() op_set_base_pickpocket_mod();

void __declspec() op_set_critter_skill_mod();

void __declspec() op_set_base_skill_mod();

void __declspec() op_set_skill_max();

void __declspec() op_set_stat_max();

void __declspec() op_set_stat_min();

void __declspec() op_set_pc_stat_max();

void __declspec() op_set_pc_stat_min();

void __declspec() op_set_npc_stat_max();

void __declspec() op_set_npc_stat_min();

void __declspec() op_set_xp_mod();

void __declspec() op_set_perk_level_mod();

}
}
