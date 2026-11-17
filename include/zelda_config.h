#ifndef __ZELDA_CONFIG_H__
#define __ZELDA_CONFIG_H__

#include <filesystem>
#include <string_view>
#include "ultramodern/config.hpp"
#include "recomp_input.h"

namespace zelda64 {
    constexpr std::u8string_view program_id = u8"MegaMan64Recompiled";
    constexpr std::string_view program_name = "Mega Man 64: Recompiled";

    // TODO: Move loading configs to the runtime once we have a way to allow per-project customization.
    void load_config();
    void save_config();
    
    void reset_input_bindings();
    void reset_cont_input_bindings();
    void reset_kb_input_bindings();
    void reset_single_input_binding(recomp::InputDevice device, recomp::GameInput input);

    std::filesystem::path get_app_folder_path();
    
    bool get_debug_mode_enabled();
    void set_debug_mode_enabled(bool enabled);
    
    enum class FilmGrainMode {
        On,
        Off,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(zelda64::FilmGrainMode, {
        {zelda64::FilmGrainMode::On, "On"},
        {zelda64::FilmGrainMode::Off, "Off"}
    });

    enum class TargetingMode {
        Switch,
        Hold,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(zelda64::TargetingMode, {
        {zelda64::TargetingMode::Switch, "Switch"},
        {zelda64::TargetingMode::Hold, "Hold"}
    });

    TargetingMode get_targeting_mode();
    void set_targeting_mode(TargetingMode mode);

    enum class AimInvertMode {
        On,
        Off,
        OptionCount
    };

    enum class RadioBoxMode {
        Expand,
        Original,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(zelda64::RadioBoxMode, {
        {zelda64::RadioBoxMode::Expand, "Expand"},
        {zelda64::RadioBoxMode::Original, "Original"}
    });

    NLOHMANN_JSON_SERIALIZE_ENUM(zelda64::AimInvertMode, {
        {zelda64::AimInvertMode::On, "On"},
        {zelda64::AimInvertMode::Off, "Off"},
    });

    RadioBoxMode get_radio_comm_box_mode();
    void set_radio_comm_box_mode(RadioBoxMode mode);

    AimInvertMode get_analog_camera_invert_mode();
    void set_analog_camera_invert_mode(AimInvertMode mode);

    enum class AnalogCamMode {
        On,
        Off,
		OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(zelda64::AnalogCamMode, {
        {zelda64::AnalogCamMode::On, "On"},
        {zelda64::AnalogCamMode::Off, "Off"}
    });

    // Values are shared with patches/mouse_camera.h.
    enum class MouseCameraMode {
        Off,
        ThirdPerson,
        FirstPerson,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(zelda64::MouseCameraMode, {
        {zelda64::MouseCameraMode::Off, "Off"},
        {zelda64::MouseCameraMode::ThirdPerson, "ThirdPerson"},
        {zelda64::MouseCameraMode::FirstPerson, "FirstPerson"}
    });

    enum class FirstPersonStrafeMode {
        On,
        Off,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(zelda64::FirstPersonStrafeMode, {
        {zelda64::FirstPersonStrafeMode::On, "On"},
        {zelda64::FirstPersonStrafeMode::Off, "Off"}
    });

    // Fog in first person: the game's own fog doesn't survive the close near plane, so it is either off or a light haze.
    // Values are shared with patches/mouse_camera.h.
    enum class FirstPersonFogMode {
        Off,
        Subtle,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(zelda64::FirstPersonFogMode, {
        {zelda64::FirstPersonFogMode::Off, "Off"},
        {zelda64::FirstPersonFogMode::Subtle, "Subtle"}
    });

    FirstPersonFogMode get_first_person_fog_mode();
    void set_first_person_fog_mode(FirstPersonFogMode mode);

    // Off plays like the base port: the modern camera reads as Off and the patches skip the layout, sprint, dodge and
    // prompts (recomp_get_definitive). Values are shared with patches/mouse_camera.h.
    enum class DefinitiveMode {
        On,
        Off,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(zelda64::DefinitiveMode, {
        {zelda64::DefinitiveMode::On, "On"},
        {zelda64::DefinitiveMode::Off, "Off"}
    });

    DefinitiveMode get_definitive_mode();
    void set_definitive_mode(DefinitiveMode mode);

    // Texture filtering. NearestTerrain is bilinear everywhere except the ground and walls, which draw point sampled so
    // the small clamped windows they are built from meet without a filtered border. Values shared with patches/gfx_patches.c.
    enum class TextureFilterMode {
        ThreePoint,
        Bilinear,
        NearestTerrain,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(zelda64::TextureFilterMode, {
        {zelda64::TextureFilterMode::ThreePoint, "ThreePoint"},
        {zelda64::TextureFilterMode::Bilinear, "Bilinear"},
        {zelda64::TextureFilterMode::NearestTerrain, "NearestTerrain"}
    });

    TextureFilterMode get_texture_filter_mode();
    void set_texture_filter_mode(TextureFilterMode mode);

    FirstPersonStrafeMode get_first_person_strafe_mode();
    void set_first_person_strafe_mode(FirstPersonStrafeMode mode);

    // Horizontal field of view in degrees (at 4:3) used while the mouse camera is on.
    int get_mouse_camera_fov();
    void set_mouse_camera_fov(int fov);

    // The mode in effect: Off while Definitive Features is Off. The setting itself is get_mouse_camera_mode_setting.
    MouseCameraMode get_mouse_camera_mode();
    MouseCameraMode get_mouse_camera_mode_setting();
    void set_mouse_camera_mode(MouseCameraMode mode);
    // Swaps between first and third person. Does nothing while the mouse camera is off.
    void toggle_mouse_camera_perspective();

    FilmGrainMode get_film_grain_mode();
    void set_film_grain_mode(FilmGrainMode mode);

    AimInvertMode get_invert_y_axis_mode();
    void set_invert_y_axis_mode(AimInvertMode mode);

    void open_quit_game_prompt();
};

#endif
