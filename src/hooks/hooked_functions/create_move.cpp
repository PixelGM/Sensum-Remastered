#include "../hooks.h"
#include "../../settings/globals.h"
#include "../../settings/options.hpp"
#include "../../helpers/input.h"
#include "../../helpers/console.h"
#include "../../helpers/entities.h"
#include "../../features/features.h"
#include "../../valve_sdk/classids.h"

#include <algorithm>

namespace hooks
{
	bool __fastcall create_move::hooked(void* ecx, void* edx, float input_sample_frametime, CUserCmd* cmd)
	{
		if (!cmd || !cmd->command_number)
			return original(ecx, input_sample_frametime, cmd);

		if (original(ecx, input_sample_frametime, cmd))
		{
			cmd->viewangles.NormalizeClamp();

			g::engine_client->SetViewAngles(cmd->viewangles);
			g::prediction->SetLocalViewAngles(cmd->viewangles);
		}

		entities::fetch_targets(cmd);
		entities::fetch_aimbot_target_info(cmd);

		if (settings::visuals::grenade_prediction)
			grenade_prediction::fetch_points(cmd);

		if (settings::misc::fast_stop)
			features::fast_stop(cmd);

		if (settings::misc::bhop)
			features::bhop(cmd);

		if (settings::misc::auto_strafe)
			features::auto_strafe(cmd);

		if (settings::misc::selfnade)
			features::selfnade(cmd);

		if (cmd->weaponselect == 0)
		{
			aimbot::handle(cmd);

			if (settings::misc::smoke_helper)
				visuals::SmokeHelperAimbot(cmd);

			if (settings::misc::flash_helper)
				visuals::PopflashHelperAimbot(cmd);
		}

		cmd->viewangles.NormalizeClamp();

		cmd->forwardmove = std::clamp(cmd->forwardmove, -450.0f, 450.0f);
		cmd->sidemove = std::clamp(cmd->sidemove, -450.0f, 450.0f);
		cmd->upmove = std::clamp(cmd->upmove, -320.0f, 320.0f);

		return original(ecx, input_sample_frametime, cmd);
	}
}