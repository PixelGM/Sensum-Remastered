#include "../render.h"
#include "../../settings/globals.h"
#include "../../settings/settings.h"
#include "../../helpers/notifies.h"
#include "../../render/menu_strings.h"

extern void bind_button(const char* label, int& key);
extern bool Hotkey(const char* label, int* k, const ImVec2& size_arg = ImVec2(0.f, 0.f));

namespace render
{
	namespace menu
	{
		extern std::map<int, weapon_type_t> get_weapons(bool need_knife);
		extern std::map<int, const char*> get_groups(bool need_knife = false, bool need_groups = false);
		extern bool selectable_weapons(
			int& selected_item,
			bool only_groups,
			std::string& weaponName,
			std::map<int, const char*> groups,
			std::map<int, weapon_type_t> k_item_names,
			std::vector<int> selected_weapons = { }
		);

		extern bool listbox_group_weapons(
			int& selected_item,
			std::map<int, const char*> groups,
			std::map<int, weapon_type_t> items,
			ImVec2 listbox_size,
			bool show_only_selected = false,
			std::vector<int> selected_weapons = { }
		);

		void aimbot_tab()
		{
			static int definition_index = 7;
			auto settings = &settings::aimbot::m_items[definition_index];

			child("Main", [&settings]()
				{
					auto k_item_names = get_weapons(false);

					const char* setting_types[] =
					{
						"Separately",
						"Subgroups",
						"For all",
						"Groups"
					};

					static bool is_settings_visible = false;

					if (ImGui::Combo("##setting_type", &settings::aimbot::setting_type, setting_types, IM_ARRAYSIZE(setting_types)))
						is_settings_visible = settings::aimbot::setting_type == settings_type_t::groups ? false : true;

					if (settings::aimbot::setting_type != settings_type_t::groups)
						is_settings_visible = true;

					if (settings::aimbot::setting_type == settings_type_t::for_all)
					{
						definition_index = 0;
					}
					else if (settings::aimbot::setting_type == settings_type_t::separately || settings::aimbot::setting_type == settings_type_t::subgroups)
					{
						auto groups = get_groups(false, true);

						std::string weaponName;
						if (settings::aimbot::setting_type == settings_type_t::subgroups)
						{
							if (groups.count(definition_index) == 0)
								definition_index = WEAPONTYPE_PISTOL;

							weaponName = groups[definition_index];
						}
						else
						{
							if (k_item_names.count(definition_index) == 0)
								definition_index = WEAPON_AK47;

							weaponName = k_item_names[definition_index].name;
						}

						selectable_weapons(definition_index, settings::aimbot::setting_type == settings_type_t::subgroups, weaponName, groups, k_item_names);
					}
					else if (settings::aimbot::setting_type == settings_type_t::groups)
					{
						if (definition_index < 0 || definition_index >= settings::aimbot::m_groups.size())
						{
							definition_index = 0;
						}

						if (!settings::aimbot::m_groups.empty())
						{
							ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth() - 70.f);
							ImGui::Combo("##aimbot.groups", &definition_index, [](void* data, int idx, const char** out_text)
								{
									*out_text = settings::aimbot::m_groups[idx].name.c_str();
									return true;
								}, nullptr, settings::aimbot::m_groups.size(), 10);
							ImGui::PopItemWidth();

							ImGui::SameLine();

							if (is_settings_visible)
							{
								if (ImGui::Button("Edit", ImVec2(ImGui::GetContentRegionAvailWidth(), 0.f)))
									is_settings_visible = false;
							}
							else
							{
								if (ImGui::Button("Hide", ImVec2(ImGui::GetContentRegionAvailWidth(), 0.f)))
									is_settings_visible = true;
							}
						}

						if (!is_settings_visible)
						{
							separator("New group");

							ImGui::Text("Name");

							static char group_name[32];
							ImGui::InputText("##aimbot.group_name", group_name, sizeof(group_name));

							if (ImGui::Button("Create"))
							{
								if (strlen(group_name) == 0)
									notifies::push("Enter the group name", notify_state_s::danger_state);
								else
								{
									settings::aimbot::m_groups.emplace_back(aimbot_group{ std::string(group_name), { } });

									memset(group_name, 0, sizeof(group_name));
									notifies::push("Group created");

									definition_index = settings::aimbot::m_groups.size() - 1;
								}
							}

							if (settings::aimbot::m_groups.empty())
								return;

							separator("Current group");

							auto& current_group = settings::aimbot::m_groups[definition_index];

							static auto weapon_to_select = -1;
							std::string placeholder = "Select weapon";

							const auto groups = get_groups(false, false);

							if (selectable_weapons(weapon_to_select, false, placeholder, groups, k_item_names, current_group.weapons))
							{
								if (std::find(current_group.weapons.begin(), current_group.weapons.end(), weapon_to_select) == current_group.weapons.end())
									current_group.weapons.emplace_back(weapon_to_select);

								weapon_to_select = -1;
							}

							static int weapon_to_remove = -1;
							if (listbox_group_weapons(weapon_to_remove, groups, k_item_names, ImVec2(0, 150.f), true, current_group.weapons))
							{
								current_group.weapons.erase(std::find(current_group.weapons.begin(), current_group.weapons.end(), weapon_to_remove));

								weapon_to_remove = -1;
							}

							if (ImGui::Button("Delete"))
							{
								notifies::push("Group removed");

								settings::aimbot::m_groups.erase(settings::aimbot::m_groups.begin() + definition_index);

								definition_index = 0;
							}

							return;
						}
					}

					checkbox("Enabled", &settings->enabled);

					if (settings::aimbot::setting_type == settings_type_t::separately)
					{
						switch (definition_index)
						{
						case WEAPON_P250:
						case WEAPON_USP_SILENCER:
						case WEAPON_GLOCK:
						case WEAPON_FIVESEVEN:
						case WEAPON_TEC9:
						case WEAPON_DEAGLE:
						case WEAPON_ELITE:
						case WEAPON_HKP2000:
							checkbox("Auto Pistol", &settings->autopistol);
							break;
						default:
							break;
						}
					}
					else if (settings::aimbot::setting_type == settings_type_t::subgroups && (definition_index == WEAPONTYPE_PISTOL || definition_index == 201))
						checkbox("Auto Pistol", &settings->autopistol);
					else
						checkbox("Auto Pistol", &settings->autopistol);

					checkbox("Air Check", &settings->check_air);
					checkbox("Flash Check", &settings->check_flash);
					checkbox("Smoke Check", &settings->check_smoke);

					if (settings::aimbot::setting_type == settings_type_t::separately)
					{
						if (utils::is_sniper(definition_index))
							checkbox("Zoom Check", &settings->check_zoom);
					}
					else if (settings::aimbot::setting_type == settings_type_t::subgroups)
					{
						if (definition_index == 240 || definition_index == 209 || definition_index == WEAPONTYPE_SNIPER_RIFLE)
							checkbox("Zoom Check", &settings->check_zoom);
					}
					else if (settings::aimbot::setting_type == settings_type_t::groups)
						checkbox("Zoom Check", &settings->check_zoom);

					checkbox("Target Teammates", &settings::misc::deathmatch);
					
					columns(2);
					{
						checkbox("By Damage", &settings->by_damage);

						ImGui::NextColumn();

						ImGui::PushItemWidth(-1);
						ImGui::SliderIntLeftAligned("Min:##min_damage", &settings->min_damage, 1, 100, "%.0f HP");
						ImGui::PopItemWidth();
					}
					columns(1);

					columns(2);
					{
						checkbox("Auto Wall", &settings->autowall.enabled);

						ImGui::NextColumn();

						ImGui::PushItemWidth(-1);
						ImGui::SliderIntLeftAligned("Min:##autowall", &settings->autowall.min_damage, 1, 100, "%.0f HP");
						ImGui::PopItemWidth();
					}
					columns(1);

					checkbox("Disable Backtrack", &settings::aimbot::hello_nigga);

					separator("Delays");

					checkbox("Auto Delay", &settings->delays.auto_delay);
					ImGui::SliderIntLeftAligned("Shot Delay:", &settings->delays.before_shot, 0, 250, "%.0f ms");
					ImGui::SliderIntLeftAligned("Target Switch Delay:", &settings->delays.target_switch, 0, 1000, "%.0f ms");

					checkbox("Auto Stop", &settings->auto_stop);

					if (settings::aimbot::setting_type != 0)
						return;

					if (!utils::is_connected())
						return;

					auto& weapon = g::local_player->m_hActiveWeapon();
					if (!weapon || !weapon->IsWeapon())
						return;

					const auto item_definition_index = weapon->m_iItemDefinitionIndex();
					if (k_item_names.count(item_definition_index) == 0)
						return;

					ImGui::Separator();

					if (ImGui::Button("Current"))
						definition_index = item_definition_index;

					if (weapon)
						definition_index = item_definition_index;
				});

			ImGui::NextColumn();

			child("Customization", [&settings]()
				{
					ImGui::PushID("aimbot.other");
					{
						checkbox("Dynamic FOV", &settings->dynamic_fov);
						checkbox("Legit Backtrack", &settings->backtrack.legit);
			
						ImGui::SliderFloatLeftAligned("FOV:", &settings->fov, 0, 180.f, "%.1f \u00B0");

						ImGui::SliderFloatLeftAligned("Smooth:", &settings->smooth, 1, 30.f, "%.1f");
						
						ImGui::SliderIntLeftAligned("Backtrack:", &settings->backtrack.ticks, 0, 12, "%1.f ms");
						
						ImGui::SliderIntLeftAligned("Hit Chance:", &settings->min_hitchance, 0, 100, "%.0f%%");
					}
					ImGui::PopID();

					separator("Hitboxes");

					columns(3);
					{
						checkbox("Head", &settings->hitboxes.head);
						checkbox("Arms", &settings->hitboxes.arms);

						ImGui::NextColumn();

						checkbox("Neck", &settings->hitboxes.neck);
						checkbox("Legs", &settings->hitboxes.legs);

						ImGui::NextColumn();

						checkbox("Body", &settings->hitboxes.body);
					}
					columns(1);

					separator("Aim Assist");

					checkbox("Spray Override##AimAssist", &settings->fov_smooth_override);

					if (settings->fov_smooth_override)
					{
						ImGui::SliderIntLeftAligned("Shots for Activation:", &settings->min_shots_fired, 3, 25, "%1.f shots");

						ImGui::SliderFloatLeftAligned("Override FOV: (?)", &settings->extended_fov, 0.f, 180.f, "%.1f \u00B0");
						tooltip("Sets FOV to X when spraying over Y shots");

						ImGui::SliderFloatLeftAligned("Override Smooth: (?)", &settings->extended_smooth, 1.f, 30.f, "%.1f");
						tooltip("Sets smooth to X when spraying over Y shots");
					}
				});

			ImGui::NextColumn();

			child("Recoil Control System", [&settings]()
				{
					checkbox("Enabled", &settings->recoil.enabled);
					checkbox("First Bullet", &settings->recoil.first_bullet);
					checkbox("Humanization", &settings->recoil.humanized);
					checkbox("Target Chest While Spraying", &settings->rcs_override_hitbox);
					
					ImGui::SliderIntLeftAligned("Pitch:", &settings->recoil.pitch, 0, 100);
					ImGui::SliderIntLeftAligned("Yaw:", &settings->recoil.yaw, 0, 100);

					separator("Trigger");

					columns(2);
					{
						checkbox("Enabled##trigger", &settings->trigger.enabled);

						ImGui::NextColumn();

						ImGui::PushItemWidth(-1);
						Hotkey("##binds.trigger", &globals::binds::trigger);
						ImGui::PopItemWidth();

						ImGui::NextColumn();

						ImGui::PushItemWidth(-1);
						checkbox("Magnet##trigger", &settings->trigger.magnet);
						ImGui::PopItemWidth();
					}
					columns(1);

					ImGui::SliderIntLeftAligned("Reaction time:", &settings->trigger.delay, 0, 250, "%.0f ms");
					ImGui::SliderIntLeftAligned("Delay Between Shots:", &settings->trigger.delay_btw_shots, 0, 250, "%.0f ms");
					ImGui::SliderIntLeftAligned("Hit Chance:", &settings->trigger.hitchance, 1, 100, "%.0f%%");
				});
		}
	}
}