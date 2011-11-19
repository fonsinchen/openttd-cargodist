/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file ai_object.cpp Implementation of AIObject. */

#include "../../stdafx.h"
#include "../../script/squirrel.hpp"
#include "../../command_func.h"
#include "../../network/network.h"
#include "../../tunnelbridge.h"

#include "../ai_storage.hpp"
#include "../ai_instance.hpp"
#include "ai_error.hpp"

/**
 * Get the storage associated with the current AIInstance.
 * @return The storage.
 */
static AIStorage *GetStorage()
{
	return AIObject::GetActiveInstance()->GetStorage();
}


/* static */ AIInstance *AIObject::ActiveInstance::active = NULL;

AIObject::ActiveInstance::ActiveInstance(AIInstance *instance)
{
	this->last_active = AIObject::ActiveInstance::active;
	AIObject::ActiveInstance::active = instance;
}

AIObject::ActiveInstance::~ActiveInstance()
{
	AIObject::ActiveInstance::active = this->last_active;
}

/* static */ AIInstance *AIObject::GetActiveInstance()
{
	assert(AIObject::ActiveInstance::active != NULL);
	return AIObject::ActiveInstance::active;
}


/* static */ void AIObject::SetDoCommandDelay(uint ticks)
{
	assert(ticks > 0);
	GetStorage()->delay = ticks;
}

/* static */ uint AIObject::GetDoCommandDelay()
{
	return GetStorage()->delay;
}

/* static */ void AIObject::SetDoCommandMode(AIModeProc *proc, AIObject *instance)
{
	GetStorage()->mode = proc;
	GetStorage()->mode_instance = instance;
}

/* static */ AIModeProc *AIObject::GetDoCommandMode()
{
	return GetStorage()->mode;
}

/* static */ AIObject *AIObject::GetDoCommandModeInstance()
{
	return GetStorage()->mode_instance;
}

/* static */ void AIObject::SetDoCommandCosts(Money value)
{
	GetStorage()->costs = CommandCost(value);
}

/* static */ void AIObject::IncreaseDoCommandCosts(Money value)
{
	GetStorage()->costs.AddCost(value);
}

/* static */ Money AIObject::GetDoCommandCosts()
{
	return GetStorage()->costs.GetCost();
}

/* static */ void AIObject::SetLastError(AIErrorType last_error)
{
	GetStorage()->last_error = last_error;
}

/* static */ AIErrorType AIObject::GetLastError()
{
	return GetStorage()->last_error;
}

/* static */ void AIObject::SetLastCost(Money last_cost)
{
	GetStorage()->last_cost = last_cost;
}

/* static */ Money AIObject::GetLastCost()
{
	return GetStorage()->last_cost;
}

/* static */ void AIObject::SetRoadType(RoadType road_type)
{
	GetStorage()->road_type = road_type;
}

/* static */ RoadType AIObject::GetRoadType()
{
	return GetStorage()->road_type;
}

/* static */ void AIObject::SetRailType(RailType rail_type)
{
	GetStorage()->rail_type = rail_type;
}

/* static */ RailType AIObject::GetRailType()
{
	return GetStorage()->rail_type;
}

/* static */ void AIObject::SetLastCommandRes(bool res)
{
	GetStorage()->last_command_res = res;
	/* Also store the results of various global variables */
	SetNewVehicleID(_new_vehicle_id);
	SetNewSignID(_new_sign_id);
	SetNewTunnelEndtile(_build_tunnel_endtile);
	SetNewGroupID(_new_group_id);
}

/* static */ bool AIObject::GetLastCommandRes()
{
	return GetStorage()->last_command_res;
}

/* static */ void AIObject::SetNewVehicleID(VehicleID vehicle_id)
{
	GetStorage()->new_vehicle_id = vehicle_id;
}

/* static */ VehicleID AIObject::GetNewVehicleID()
{
	return GetStorage()->new_vehicle_id;
}

/* static */ void AIObject::SetNewSignID(SignID sign_id)
{
	GetStorage()->new_sign_id = sign_id;
}

/* static */ SignID AIObject::GetNewSignID()
{
	return GetStorage()->new_sign_id;
}

/* static */ void AIObject::SetNewTunnelEndtile(TileIndex tile)
{
	GetStorage()->new_tunnel_endtile = tile;
}

/* static */ TileIndex AIObject::GetNewTunnelEndtile()
{
	return GetStorage()->new_tunnel_endtile;
}

/* static */ void AIObject::SetNewGroupID(GroupID group_id)
{
	GetStorage()->new_group_id = group_id;
}

/* static */ GroupID AIObject::GetNewGroupID()
{
	return GetStorage()->new_group_id;
}

/* static */ void AIObject::SetAllowDoCommand(bool allow)
{
	GetStorage()->allow_do_command = allow;
}

/* static */ bool AIObject::GetAllowDoCommand()
{
	return GetStorage()->allow_do_command;
}

/* static */ bool AIObject::CanSuspend()
{
	Squirrel *squirrel = AIObject::GetActiveInstance()->engine;
	return GetStorage()->allow_do_command && squirrel->CanSuspend();
}

/* static */ void *&AIObject::GetEventPointer()
{
	return GetStorage()->event_data;
}

/* static */ void *&AIObject::GetLogPointer()
{
	return GetStorage()->log_data;
}

/* static */ void AIObject::SetCallbackVariable(int index, int value)
{
	if ((size_t)index >= GetStorage()->callback_value.size()) GetStorage()->callback_value.resize(index + 1);
	GetStorage()->callback_value[index] = value;
}

/* static */ int AIObject::GetCallbackVariable(int index)
{
	return GetStorage()->callback_value[index];
}

/* static */ bool AIObject::DoCommand(TileIndex tile, uint32 p1, uint32 p2, uint cmd, const char *text, AISuspendCallbackProc *callback)
{
	if (!AIObject::CanSuspend()) {
		throw AI_FatalError("You are not allowed to execute any DoCommand (even indirect) in your constructor, Save(), Load(), and any valuator.");
	}

	/* Set the default callback to return a true/false result of the DoCommand */
	if (callback == NULL) callback = &AIInstance::DoCommandReturn;

	/* Are we only interested in the estimate costs? */
	bool estimate_only = GetDoCommandMode() != NULL && !GetDoCommandMode()();

#ifdef ENABLE_NETWORK
	/* Only set p2 when the command does not come from the network. */
	if (GetCommandFlags(cmd) & CMD_CLIENT_ID && p2 == 0) p2 = UINT32_MAX;
#endif

	/* Try to perform the command. */
	CommandCost res = ::DoCommandPInternal(tile, p1, p2, cmd, _networking ? CcAI : NULL, text, false, estimate_only);

	/* We failed; set the error and bail out */
	if (res.Failed()) {
		SetLastError(AIError::StringToError(res.GetErrorMessage()));
		return false;
	}

	/* No error, then clear it. */
	SetLastError(AIError::ERR_NONE);

	/* Estimates, update the cost for the estimate and be done */
	if (estimate_only) {
		IncreaseDoCommandCosts(res.GetCost());
		return true;
	}

	/* Costs of this operation. */
	SetLastCost(res.GetCost());
	SetLastCommandRes(true);

	if (_networking) {
		/* Suspend the AI till the command is really executed. */
		throw AI_VMSuspend(-(int)GetDoCommandDelay(), callback);
	} else {
		IncreaseDoCommandCosts(res.GetCost());

		/* Suspend the AI player for 1+ ticks, so it simulates multiplayer. This
		 *  both avoids confusion when a developer launched his AI in a
		 *  multiplayer game, but also gives time for the GUI and human player
		 *  to interact with the game. */
		throw AI_VMSuspend(GetDoCommandDelay(), callback);
	}

	NOT_REACHED();
}
