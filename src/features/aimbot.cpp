#include "features.h"
#include "../helpers/autowall.h"
#include "../settings/globals.h"
#include "../helpers/input.h"
#include "../helpers/console.h"
#include "../helpers/entities.h"
#include "..\\hooks\hooks.h"
#include "..//helpers/notifies.h"

namespace aimbot
{
	bool using_trigger = false;

	weapon_setting a_setting;

	Ray_t ray;
	CGameTrace tr;
	CCSWeaponInfo* weapon_data;
	c_base_combat_weapon* weapon;
	
	namespace punches
	{
		QAngle current = { 0, 0, 0 };

		QAngle last = { 0, 0, 0 };
	}

	struct player_t
	{
		int index;
		int hitbox;

		float fov;
		float sim_time;

		Vector origin;
		QAngle angles;
		QAngle aim_angle;
		entities::hitbox_t point;
		
		c_base_player* player;

		float damage = 0.f;

		const bool operator<(const player_t& other)
		{
			if (!a_setting.backtrack.legit || (using_trigger && !a_setting.trigger.magnet))
				return fov < other.fov;

			const auto a = TIME_TO_TICKS(sim_time);
			const auto b = TIME_TO_TICKS(other.sim_time);

			if (a == b)
				return fov < other.fov;

			return a > b;
		}
	};

	struct player_pos_t
	{
		int id;
		float sim_time;

		Vector origin;
		Vector eye_pos;
	};

	namespace delays
	{
		ULONGLONG last_scope_time = 0;
		ULONGLONG last_aiming_time = 0;

		auto reaction_pause = false;
		ULONGLONG reaction_end_time = 0;
	}

	namespace network
	{
		float out_delay = 0.f;
		float network_delay = 0.f;
		float latency_delay = 0.f;
		float next_server_time = 0.f;
		float interpolation_time = 0.f;

		const float get_interpolation_time()
		{
			static const auto cl_interp = g::cvar->find(xorstr_("cl_interp"));
			static const auto c_min_ratio = g::cvar->find(xorstr_("sv_client_min_interp_ratio"));
			static const auto c_max_ratio = g::cvar->find(xorstr_("sv_client_max_interp_ratio"));

			static const auto max_ud_rate = g::cvar->find(xorstr_("sv_maxupdaterate"));
			const auto ud_rate = max_ud_rate->GetInt();

			static const auto cl_interp_ratio = g::cvar->find(xorstr_("cl_interp_ratio"));
			float ratio = cl_interp_ratio->GetFloat();

			if (ratio == 0)
				ratio = 1.0f;

			if (c_min_ratio && c_max_ratio && c_min_ratio->GetFloat() != 1)
				ratio = std::clamp(ratio, c_min_ratio->GetFloat(), c_max_ratio->GetFloat());

			return std::max(cl_interp->GetFloat(), (ratio / ud_rate));
		}

		void fetch_delays(const int& tick_count)
		{
			const auto nci = g::engine_client->GetNetChannelInfo();

			static const auto sv_maxunlag = g::cvar->find(xorstr_("sv_maxunlag"));

			auto unlag = sv_maxunlag->GetFloat();

			auto copy = sv_maxunlag->GetFloat();

			interpolation_time = get_interpolation_time();

			latency_delay = nci->GetLatency(FLOW_OUTGOING) + nci->GetLatency(FLOW_INCOMING);
			network_delay = std::clamp(latency_delay + interpolation_time, 0.f, unlag);
			out_delay = std::clamp(nci->GetLatency(FLOW_OUTGOING) + interpolation_time, 0.f, unlag);

			next_server_time = TICKS_TO_TIME(tick_count + 1) + network_delay;
		}
	}

	void reset_vars()
	{
		delays::reaction_pause = false;
		
		delays::last_aiming_time = 0;
		delays::last_scope_time = 0;
		delays::reaction_end_time = 0;

		//punches::current = { 0, 0, 0 };
		//punches::last = { 0, 0, 0 };
	}

	void slow_move(CUserCmd* cmd, const float& amount)
	{
		Vector forward;
		QAngle direction;

		const auto& velocity = g::local_player->m_vecVelocity();
		const auto& speed = velocity.Length2D();

		math::vector2angles(velocity, direction);

		direction.yaw = cmd->viewangles.yaw - direction.yaw;

		math::angle2vectors(direction, forward);
		const auto source = forward * -speed;

		const auto max_speed = 250.f;
		if (speed >= max_speed * 0.0034f * amount)
		{
			cmd->forwardmove = source.x;
			cmd->sidemove = source.y;
		}
	}

	void autopistol(CUserCmd* cmd)
	{
		if (weapon_data->WeaponType == WEAPONTYPE_PISTOL && a_setting.autopistol)
			if (!weapon->CanFire())
				cmd->buttons &= ~IN_ATTACK;
	}

	bool is_enabled(CUserCmd* cmd)
	{
		using_trigger = false;
		
		if (!g::local_player || !g::local_player->IsAlive())
			return false;

		weapon = g::local_player->m_hActiveWeapon();
		if (!weapon || !weapon->IsWeapon() || !weapon->HasBullets())
			return false;

		weapon_data = weapon->GetWeaponData();
		if (weapon_data->WeaponType == WEAPONTYPE_SNIPER_RIFLE)
		{
			if (!g::local_player->m_bIsScoped())
				delays::last_scope_time = GetTickCount64();
		}

		int index = 0;
		if (settings::aimbot::setting_type == settings_type_t::separately)
			index = weapon->m_iItemDefinitionIndex();
		else if (settings::aimbot::setting_type == settings_type_t::subgroups)
		{
			switch (weapon->m_iItemDefinitionIndex())
			{
			case WEAPON_AWP:
			case WEAPON_SSG08:
			case WEAPON_DEAGLE:
				index = 200 + weapon->m_iItemDefinitionIndex();
				break;
			default:
				index = weapon->GetWeaponData()->WeaponType;
			}
		}
		else if (settings::aimbot::setting_type == settings_type_t::groups)
		{
			int group_index = -1;
			for (size_t k = 0; k < settings::aimbot::m_groups.size(); k++)
			{
				const auto& group = settings::aimbot::m_groups[k];
				if (std::find(group.weapons.begin(), group.weapons.end(), weapon->m_iItemDefinitionIndex()) != group.weapons.end())
				{
					group_index = k;
					break;
				}
			}

			if (group_index == -1)
				return false;
		}

		a_setting = settings::aimbot::m_items[index];
		if (!a_setting.enabled)
			return false;

		const auto is_recoil_enabled = a_setting.recoil.enabled && (a_setting.recoil.pitch > 0 || a_setting.recoil.yaw > 0);
		if (a_setting.fov <= 0.f)
		{
			if ((!a_setting.trigger.enabled || a_setting.trigger.magnet) && !is_recoil_enabled)
				return false;
		}

		if (!(cmd->buttons & IN_ATTACK))
		{
			using_trigger = a_setting.trigger.enabled && globals::binds::trigger != 0 && (GetAsyncKeyState(globals::binds::trigger) & 0x8000);
			if (!using_trigger)
				return false;
		}

		if (a_setting.check_zoom && weapon_data->WeaponType == WEAPONTYPE_SNIPER_RIFLE)
			return g::local_player->m_bIsScoped() && delays::last_scope_time + 60 <= GetTickCount64();

		return true;
	}

	int get_hitgroup(const int& hitbox)
	{
		switch (hitbox)
		{
		case HITBOX_HEAD:
		case HITBOX_NECK:
			return 1;

		case HITBOX_THORAX:
		case HITBOX_LOWER_CHEST:
		case HITBOX_UPPER_CHEST:
			return 2;

		case HITBOX_PELVIS:
		case HITBOX_BELLY:
			return 3;

		case HITBOX_RIGHT_HAND:
		case HITBOX_RIGHT_UPPER_ARM:
		case HITBOX_RIGHT_FOREARM:
			//return 4;

		case HITBOX_LEFT_HAND:
		case HITBOX_LEFT_UPPER_ARM:
		case HITBOX_LEFT_FOREARM:
			return 5;

		case HITBOX_RIGHT_THIGH:
		case HITBOX_RIGHT_CALF:
		case HITBOX_RIGHT_FOOT:
			//return 6;

		case HITBOX_LEFT_THIGH:
		case HITBOX_LEFT_CALF:
		case HITBOX_LEFT_FOOT:
			return 7;
		}

		return -1;
	}

	bool is_suitable_by_min_damage(c_base_player* entity, const int& hitbox, const int& min_damage)
	{
		if (min_damage < 1.f || min_damage > 100.f)
			return true;

		const auto hitgroup = get_hitgroup(hitbox);
		if (hitgroup == -1)
			return false;

		auto damage = float(weapon_data->iDamage);
		autowall::scale_damage(hitgroup, entity, weapon_data->flArmorRatio, weapon_data->fHeadshotMultiplier, damage);
		
		return damage >= min_damage || damage >= entity->m_iHealth();
	}

	Vector sphere_ray_intersection(const Vector& start, const Vector& end, const Vector& center, const float& radius)
	{
		const auto direction = (end - start).Normalized();

		const auto q = center - start;
		const auto v = q.Dot(direction);
		const auto d = radius * radius - (q.LengthSqr() - v * v);

		return d < FLT_EPSILON ? Vector{} : start + direction * (v - std::sqrt(d));
	}

	bool intersects_point(const Vector& start, const Vector& end, const Vector& mins, const Vector& maxs, float radius)
	{
		const auto delta = (maxs - mins).Normalized();
		if (sphere_ray_intersection(start, end, maxs, radius).IsValid())
			return true;

		for (size_t i{}; i < std::floor(mins.DistTo(maxs)); ++i)
		{
			if (sphere_ray_intersection(start, end, mins + delta * float(i), radius).IsValid())
				return true;
		}

		return false;
	}

	bool is_spraying()
	{
		return a_setting.recoil.enabled && ((a_setting.recoil.pitch > 0 || a_setting.recoil.yaw > 0)) && g::local_player->m_iShotsFired() > (a_setting.recoil.first_bullet ? 0 : 1);
	}

	std::vector<int> hitboxes()
	{
		static std::vector<int> list;
		list.clear();

		auto a_settings = &settings::aimbot::m_items[weapon->m_iItemDefinitionIndex()];

		if (a_settings->hitboxes.head)
			list.emplace_back(HITBOX_HEAD);

		if (a_settings->hitboxes.neck)
			list.emplace_back(HITBOX_NECK);

		if (a_settings->hitboxes.body)
		{
			list.emplace_back(HITBOX_UPPER_CHEST);
			list.emplace_back(HITBOX_LOWER_CHEST);
			list.emplace_back(HITBOX_THORAX);
			list.emplace_back(HITBOX_BELLY);
			list.emplace_back(HITBOX_PELVIS);
		}

		if (a_settings->hitboxes.arms)
		{
			list.emplace_back(HITBOX_RIGHT_UPPER_ARM);
			list.emplace_back(HITBOX_RIGHT_FOREARM);
			list.emplace_back(HITBOX_LEFT_UPPER_ARM);
			list.emplace_back(HITBOX_LEFT_FOREARM);
		}

		if (a_settings->hitboxes.legs)
		{
			list.emplace_back(HITBOX_RIGHT_THIGH);
			list.emplace_back(HITBOX_LEFT_THIGH);
			list.emplace_back(HITBOX_RIGHT_CALF);
			list.emplace_back(HITBOX_LEFT_CALF);
		}

		return list;
	}

	// loop thru players
	// get their hitboxes
	// save their hitboxes to stl map
	//
	
	bool sort_by_distance(const player_t& first, const player_t& second)
	{
		const Vector& local_origin = g::local_player->m_vecOrigin();

		float dist1_to_local = first.origin.DistTo(local_origin);
		float dist2_to_local = second.origin.DistTo(local_origin);

		return dist1_to_local < dist2_to_local;
	}

	std::vector<player_t>& get_targets(QAngle& view_angles, const Vector& eye_pos, int& tick_count, const int& min_damage)
	{
		static std::vector<player_t> targets;
		targets.clear();

		static std::vector<player_pos_t> duplicates;
		duplicates.clear();

		const auto max_time = TICKS_TO_TIME(std::clamp<int>(a_setting.backtrack.ticks, 0, 12) + 1);

		const auto& hitbox_ids = hitboxes();

		if (hitbox_ids.empty())
			return targets;
		
		QAngle aim_angles;
		for (const auto& entity : entities::m_targets)
		{
			if (entity.entity_index == 0 || entity.entity_index == g::local_player->GetIndex())
				continue;

			const auto delta_time = fabsf(network::out_delay - (network::next_server_time - entity.sim_time - network::interpolation_time));
			if (!entity.eye_pos.IsValid() || delta_time > 0.2f || delta_time > max_time)
				continue;

			const auto result_of_duplicates = std::find_if(duplicates.begin(), duplicates.end(), [entity](player_pos_t const& c)
				{
					const auto is_the_same = c.sim_time == entity.sim_time || (c.eye_pos == entity.eye_pos && c.origin == entity.abs_origin);

					return c.id == entity.entity_index && is_the_same;
				});

			if (result_of_duplicates != duplicates.end())
				continue;

			auto* player = c_base_player::GetPlayerByIndex(entity.entity_index);
			if (!player || !player->IsPlayer() || !player->IsAlive() || player->m_bGunGameImmunity() || player->IsDormant())
				continue;

			duplicates.push_back({ entity.entity_index, entity.sim_time, entity.eye_pos, entity.abs_origin });

			for (const auto& hitbox_id : hitbox_ids)
			{
				const auto& hitbox = entity.hitboxes[hitbox_id];
				if (!hitbox.points[0].IsValid())
					continue;

				math::vector2angles(hitbox.points[0] - eye_pos, aim_angles);

				auto fov = a_setting.dynamic_fov ? math::GetRealDistanceFOV(eye_pos.DistTo(hitbox.points[0]), view_angles, aim_angles) :
					math::GetFovToPlayer(view_angles, aim_angles);

				if (a_setting.fov_smooth_override)
				{
					float old_fov = a_setting.fov;
					float old_smooth = a_setting.smooth;

					if (g::local_player->m_iShotsFired() >= a_setting.min_shots_fired)
					{
						a_setting.fov = a_setting.extended_fov;
						a_setting.smooth = a_setting.extended_smooth;
					}
					else if (g::local_player->m_iShotsFired() < a_setting.min_shots_fired)
					{
						a_setting.fov = old_fov;
						a_setting.smooth = old_smooth;
					}
				}

				if ((!using_trigger || a_setting.trigger.magnet) && fov > a_setting.fov)
					continue;

				targets.push_back({ entity.entity_index, hitbox_id, fov, entity.sim_time, entity.abs_origin, entity.angles, aim_angles, hitbox, player });
			}
		}

		if (!targets.empty())
			std::sort(targets.begin(), targets.end(), sort_by_distance);

		return targets;
	}

	//std::vector<player_t>& get_targets(QAngle& view_angles, const Vector& eye_pos, int& tick_count, const int& min_damage)
	//{
	//	static std::vector<player_t> targets;
	//	targets.clear();

	//	static std::vector<player_pos_t> duplicates;
	//	duplicates.clear();

	//	const auto max_time = TICKS_TO_TIME(std::clamp<int>(a_setting.backtrack.ticks, 0, 12) + 1);

	//	const auto& hitbox_ids = hitboxes();

	//	QAngle aim_angles;
	//	for (const auto& entity : entities::m_players)
	//	{
	//		if (hitbox_ids.empty())
	//			continue;

	//		if (entity.index == 0 || entity.index == g::local_player->GetIndex())
	//			continue;

	//		const auto delta_time = fabsf(network::out_delay - (network::next_server_time - entity.m_flSimulationTime - network::interpolation_time));
	//		if (!entity.eye_pos.IsValid() || delta_time > 0.2f || delta_time > max_time)
	//			continue;

	//		const auto result_of_duplicates = std::find_if(duplicates.begin(), duplicates.end(), [entity](player_pos_t const& c)
	//			{
	//				const auto is_the_same = c.sim_time == entity.m_flSimulationTime || (c.eye_pos == entity.eye_pos && c.origin == entity.origin);

	//				return c.id == entity.index && is_the_same;
	//			});

	//		if (result_of_duplicates != duplicates.end())
	//			continue;

	//		auto* player = c_base_player::GetPlayerByIndex(entity.index);
	//		if (!player || !player->IsPlayer() || !player->IsAlive() || player->m_bGunGameImmunity() || player->IsDormant())
	//			continue;

	//		duplicates.push_back({ entity.index, entity.m_flSimulationTime, entity.eye_pos, entity.origin });

	//		for (const auto& hitbox_id : hitbox_ids)
	//		{
	//			const auto& hitbox = entity.hitboxes[hitbox_id];
	//			if (!hitbox.points[0].IsValid())
	//				continue;

	//			math::vector2angles(hitbox.points[0] - eye_pos, aim_angles);

	//			auto fov = a_setting.dynamic_fov ? math::GetRealDistanceFOV(eye_pos.DistTo(hitbox.points[0]), view_angles, aim_angles) :
	//				math::GetFovToPlayer(view_angles, aim_angles);

	//			if (a_setting.fov_smooth_override)
	//			{
	//				float old_fov = a_setting.fov;
	//				float old_smooth = a_setting.smooth;

	//				if (g::local_player->m_iShotsFired() >= a_setting.min_shots_fired)
	//				{
	//					a_setting.fov = a_setting.extended_fov;
	//					a_setting.smooth = a_setting.extended_smooth;
	//				}
	//				else if (g::local_player->m_iShotsFired() < a_setting.min_shots_fired)
	//				{
	//					a_setting.fov = old_fov;
	//					a_setting.smooth = old_smooth;
	//				}
	//			}

	//			if ((!using_trigger || a_setting.trigger.magnet) && fov > a_setting.fov)
	//				continue;

	//			targets.push_back({ entity.index, hitbox_id, fov, entity.m_flSimulationTime, entity.origin, entity.angles, aim_angles, hitbox, player });
	//		}
	//	}

	//	/*for (const auto& tick : entities::m_items)
	//	{
	//		if (tick.players->index == 0)
	//			continue;

	//		for (const auto& entity : tick.players)
	//		{
	//			if (entity.index == 0 || entity.index == g::local_player->GetIndex())
	//				continue;

	//			const auto delta_time = fabsf(network::out_delay - (network::next_server_time - entity.m_flSimulationTime - network::interpolation_time));
	//			if (!entity.eye_pos.IsValid() || delta_time > 0.2f || delta_time > max_time)
	//				continue;

	//			const auto result_of_duplicates = std::find_if(duplicates.begin(), duplicates.end(), [entity](player_pos_t const& c)
	//			{
	//				const auto is_the_same = c.sim_time == entity.m_flSimulationTime || (c.eye_pos == entity.eye_pos && c.origin == entity.origin);

	//				return c.id == entity.index && is_the_same;
	//			});

	//			if (result_of_duplicates != duplicates.end())
	//				continue;

	//			auto* player = c_base_player::GetPlayerByIndex(entity.index);
	//			if (!player || !player->IsPlayer() || !player->IsAlive() || player->m_bGunGameImmunity() || player->IsDormant())
	//				continue;

	//			auto hitbox_ids = player->hitboxes();
	//			if (hitbox_ids.empty())
	//				continue;

	//			duplicates.push_back({ entity.index, entity.m_flSimulationTime, entity.eye_pos, entity.origin });

	//			for (const auto& hitbox_id : hitbox_ids)
	//			{
	//				const auto& hitbox = entity.hitboxes[hitbox_id];
	//				if (!hitbox.points[0].IsValid())
	//					continue;
	//				
	//				math::vector2angles(hitbox.points[0] - eye_pos, aim_angles);

	//				auto fov = a_setting.dynamic_fov ? math::GetRealDistanceFOV(eye_pos.DistTo(hitbox.points[0]), view_angles, aim_angles) :
	//					math::GetFovToPlayer(view_angles, aim_angles);

	//				if (a_setting.fov_smooth_override)
	//				{
	//					float old_fov = a_setting.fov;
	//					float old_smooth = a_setting.smooth;

	//					if (g::local_player->m_iShotsFired() >= a_setting.min_shots_fired)
	//					{
	//						a_setting.fov = a_setting.extended_fov;
	//						a_setting.smooth = a_setting.extended_smooth;
	//					}
	//					else if (g::local_player->m_iShotsFired() < a_setting.min_shots_fired)
	//					{
	//						a_setting.fov = old_fov;
	//						a_setting.smooth = old_smooth;
	//					}
	//				}

	//				if ((!using_trigger || a_setting.trigger.magnet) && fov > a_setting.fov)
	//					continue;

	//				targets.push_back({ entity.index, hitbox_id, fov, entity.m_flSimulationTime, entity.origin, entity.angles, aim_angles, hitbox, player });
	//			}
	//		}
	//	}*/

	//	if (!targets.empty())
	//		std::sort(targets.begin(), targets.end());

	//	return targets;
	//}

	bool find_target(QAngle& view_angles, const Vector& eye_pos, int& tick_count, const int& min_damage, player_t& result)
	{
		auto& targets = get_targets(view_angles, eye_pos, tick_count, min_damage);
		if (targets.empty())
			return false;

		const auto awall_damage = std::clamp<float>(a_setting.autowall.min_damage, 0.f, 100.f);
		const bool awall_enabled = a_setting.autowall.enabled && awall_damage >= 1;

		const bool check_smoke = a_setting.check_smoke;
		const bool check_flash = a_setting.check_flash;
		const bool check_air = a_setting.check_air;
		const bool using_trigger_inner = using_trigger && !a_setting.trigger.magnet;

		QAngle old_angles;
		Vector old_origin, end_pos;

		math::angle2vectors(view_angles, end_pos);
		end_pos = eye_pos + (end_pos * weapon_data->flRange);

		static auto update_poses = [](c_base_player* player, const QAngle& angles, const Vector& origin)
		{
			player->SetAbsOrigin(origin);
			player->SetAbsAngles(angles);
			player->InvalidateBoneCache();
		};

		for (auto& entity : targets)
		{
			if (check_smoke && utils::is_line_goes_through_smoke(eye_pos, entity.point.points[0]))
				continue;

			if (check_flash && g::local_player->IsFlashed())
				continue;

			if (check_air && g::local_player->InAir())
				continue;

			if (using_trigger_inner)
			{
				if (!globals::is_visible[entity.index])
					continue;

				if (intersects_point(eye_pos, end_pos, entity.point.min, entity.point.max, entity.point.radius * 0.6f))
				{
					tick_count = TIME_TO_TICKS(entity.sim_time + network::interpolation_time);
					result = entity;
					return true;
				}

				continue;
			}

			bool pos_changed = false;
			old_origin = entity.origin;
			old_angles = entity.player->GetAbsAngles();

			CTraceFilterSkipTwoEntities filter(g::local_player, entity.player);

			for (const auto& point : entity.point.points)
			{
				if (!point.IsValid())
					continue;

				bool is_suitable_damage = !awall_enabled || (a_setting.by_damage && is_suitable_by_min_damage(entity.player, entity.hitbox, min_damage));

				if (awall_enabled && !is_suitable_damage)
					continue;

				ray.Init(eye_pos, point);
				g::engine_trace->trace_ray(ray, CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_MONSTER | CONTENTS_DEBRIS | CONTENTS_HITBOX, &filter, &tr);

				if (eye_pos.DistTo(tr.endpos) != eye_pos.DistTo(point))
				{
					if (!awall_enabled || !is_suitable_damage)
						continue;

					if (pos_changed)
						continue;

					pos_changed = true;

					update_poses(entity.player, QAngle(0.f, entity.angles.yaw, 0.f), entity.origin);

					const auto damage = autowall::get_damage(point);
					if (damage < 1.f)
						continue;

					if (a_setting.by_damage && damage < min_damage && damage < entity.player->m_iHealth())
						continue;

					entity.damage = damage;
				}

				QAngle aim_angle;
				math::vector2angles(point - eye_pos, aim_angle);
				aim_angle.NormalizeClamp();

				if (pos_changed)
					update_poses(entity.player, old_angles, old_origin);

				result = entity;
				entity.aim_angle = aim_angle;
				return true;
			}

			if (pos_changed)
				update_poses(entity.player, old_angles, old_origin);
		}

		return false;
	}

	auto hitchance(c_base_player* player, const QAngle& angles, const float& chance) -> bool
	{
		Vector forward, right, up;
		Vector src = g::local_player->GetEyePos();
		math::angle2vectors(angles, forward, right, up);

		const auto hit_count = 150.f;

		int cHits = 0;
		int cNeededHits = static_cast<int>(hit_count * (chance * 0.01f));

		weapon->UpdateAccuracyPenalty();
		float weap_spread = weapon->GetSpread();
		float weap_inaccuracy = weapon->GetInaccuracy();

		CTraceFilterPlayersOnlySkipOne filter(g::local_player);

		QAngle viewAnglesSpread;
		Vector direction, viewForward;
		for (int i = 0; i < hit_count; i++)
		{
			auto a = utils::random(0.f, 1.f);
			const auto c = utils::random(0.f, 1.f);

			if (weapon->m_iItemDefinitionIndex() == WEAPON_REVOLVER)
			{
				a = 1.f - a * a;
				a = 1.f - c * c;
			}

			const auto b = utils::random(0.f, 2.f * PI);
			const auto d = utils::random(0.f, 2.f * PI);

			const auto spread = c * weap_spread;
			const auto inaccuracy = a * weap_inaccuracy;

			Vector spreadView((cos(b) * inaccuracy) + (cos(d) * spread), (sin(b) * inaccuracy) + (sin(d) * spread), 0);

			direction.x = forward.x + (spreadView.x * right.x) + (spreadView.y * up.x);
			direction.y = forward.y + (spreadView.x * right.y) + (spreadView.y * up.y);
			direction.z = forward.z + (spreadView.x * right.z) + (spreadView.y * up.z);
			direction.Normalized();

			math::vector2angles(direction, up, viewAnglesSpread);
			viewAnglesSpread.NormalizeClamp();

			math::angle2vectors(viewAnglesSpread, viewForward);
			viewForward.NormalizeInPlace();

			viewForward = src + (viewForward * weapon_data->flRange);

			ray.Init(src, viewForward);
			g::engine_trace->trace_ray(ray, CONTENTS_SOLID | CONTENTS_HITBOX, &filter, &tr);

			if (tr.hit_entity == player)
				++cHits;

			const auto ch = int((float(cHits) / hit_count) * 100.f);
			if (ch >= chance)
				return true;

			if ((hit_count - i + cHits) < cNeededHits)
				return false;
		}

		return false;
	}

	bool able_to_rcs()
	{
		const auto& nci = g::engine_client->GetNetChannelInfo();

		if (!nci)
			return false;

		float ping = std::round((nci->GetAvgLatency(FLOW_INCOMING) + nci->GetAvgLatency(FLOW_OUTGOING)) * 1000.f);

		if (ping >= 120.f)
			return false;

		return true;
	}

	bool recoil(QAngle& angles)
	{
		//if (!is_spraying())
			//return false;

		a_setting.recoil.pitch = std::clamp<int>(a_setting.recoil.pitch, 0, 100);
		a_setting.recoil.yaw = std::clamp<int>(a_setting.recoil.yaw, 0, 100);

		/*if (a_setting.recoil.pitch == 0 && a_setting.recoil.yaw == 0)
			return false;*/

		static auto weapon_recoil_scale = g::cvar->find(xorstr_("weapon_recoil_scale"));
		float recoil_scale = weapon_recoil_scale->GetFloat();

		float yaw = (recoil_scale / 100) * a_setting.recoil.yaw;
		float pitch = (recoil_scale / 100) * a_setting.recoil.pitch;

		QAngle punch = { };

		punch.pitch = g::local_player->m_aimPunchAngle().pitch * pitch;
		punch.yaw = g::local_player->m_aimPunchAngle().yaw * yaw;

		angles -= punch;
		angles.NormalizeClamp();

		return true;
	}

	void handle(CUserCmd* cmd)
	{
		if (!is_enabled(cmd))
		{
			reset_vars();
			
			return;
		}

		network::fetch_delays(cmd->tick_count);

		QAngle angles;
		g::engine_client->GetViewAngles(angles); //cmd->viewangles;
		QAngle current = angles;
		int tick_count = cmd->tick_count;
		int min_damage = std::clamp<int>(a_setting.min_damage, 0, 100);
		const Vector eye_pos = g::local_player->GetEyePos();

		player_t result;

		static ConVar* weapon_recoil_scale = g::cvar->find("weapon_recoil_scale");
		float recoil_scale = weapon_recoil_scale->GetFloat();

		if (find_target(angles, eye_pos, tick_count, min_damage, result))
		{
			auto reset_vars_inner = [cmd, &result, &tick_count]()
			{
				tick_count = cmd->tick_count;

				//g::hud_chat->ChatPrintf(0, 0, "find_target->reset_vars called");
			};
			
			punches::current = g::local_player->m_aimPunchAngle();
			{
				recoil(angles);
				//punches::last = { 0, 0, 0 };

				angles.NormalizeClamp();
			}
			punches::last = punches::current;

			angles = result.aim_angle;
			angles -= weapon->RequiresRecoilControl() ? (g::local_player->m_aimPunchAngle() * recoil_scale).NormalizedClamp() : QAngle();

			tick_count = TIME_TO_TICKS(result.sim_time + network::interpolation_time);

			if (a_setting.by_damage)
			{
				if (result.damage > 0.f)
				{
					if (result.damage < min_damage && result.damage < result.player->m_iHealth())
						return;
				}
				else if (!is_suitable_by_min_damage(result.player, result.hitbox, min_damage))
					return;
			}

			if (using_trigger)
			{
				if (!a_setting.trigger.magnet)
				{
					if (!globals::is_visible[result.player->GetIndex()])
						return;

					Vector end_pos;
					math::angle2vectors(angles, end_pos);
					end_pos = eye_pos + (end_pos * weapon_data->flRange);

					ray.Init(eye_pos, end_pos);

					CTraceFilterSkipTwoEntities filter(g::local_player, result.player);
					g::engine_trace->trace_ray(ray, CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_MONSTER | CONTENTS_DEBRIS | CONTENTS_HITBOX, &filter, &tr);

					if (eye_pos.DistTo(tr.endpos) < eye_pos.DistTo(result.point.points[0]))
						return;
				}

				if (delays::reaction_pause)
				{
					if (delays::reaction_end_time > GetTickCount64())
						return;
				}
				else if (a_setting.trigger.delay > 0)
				{
					delays::reaction_pause = true;
					delays::reaction_end_time = GetTickCount64() + a_setting.trigger.delay;

					return;
				}

				if (a_setting.trigger.delay_btw_shots > 0 && delays::last_aiming_time + a_setting.trigger.delay_btw_shots > GetTickCount64())
					return;

				if (!weapon->CanFire())
					return;
			}

			static auto last_entity = -1;
			if (result.index != last_entity)
			{
				if (delays::last_aiming_time + a_setting.delays.target_switch > GetTickCount64()) // && !is_spraying()
					return;
			}

			const auto hit_chance = std::clamp<int>(using_trigger ? a_setting.trigger.hitchance : a_setting.min_hitchance, 0, 100);
			if (hit_chance > 0)
			{
				const auto& old_origin = result.origin;
				const auto& old_angles = result.player->GetAbsAngles();

				result.player->SetAbsOrigin(result.origin);
				result.player->SetAbsAngles(QAngle(0.f, result.angles.yaw, 0.f));
				result.player->InvalidateBoneCache();

				const auto& state = hitchance(result.player, angles, (float)hit_chance);

				result.player->SetAbsAngles(old_angles);
				result.player->SetAbsOrigin(old_origin);
				result.player->InvalidateBoneCache();

				if (!state)
				{
					if (a_setting.auto_stop)
						slow_move(cmd, 35.f);

					return;
				}
			}
			else if (a_setting.auto_stop)
			{
				slow_move(cmd, 44.f);
			}

			last_entity = result.index;
			delays::last_aiming_time = GetTickCount64();
		}

		//punches::current = g::local_player->m_aimPunchAngle();
		//{
		//	recoil(angles);				
		//	//punches::last = { 0, 0, 0 };

		//	angles.NormalizeClamp();
		//}
		//punches::last = punches::current;

		if (result.player)
			math::smooth(a_setting.smooth, current, angles, angles, a_setting.recoil.humanized);

		math::correct_movement(cmd, cmd->viewangles);

		cmd->viewangles = angles;
		g::engine_client->SetViewAngles(cmd->viewangles);

		if (using_trigger)
			cmd->buttons |= IN_ATTACK;

		autopistol(cmd);

		if (cmd->buttons & IN_ATTACK)
			cmd->tick_count = tick_count;
	}
}